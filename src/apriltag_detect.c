/*
 * apriltag_detect.c — shared AprilTag detection for ag-cam-tools
 *
 * Extracted from cmd_stream.c so that both `stream` and `capture`
 * can reuse the same detector setup and per-frame detection logic.
 */

#include "apriltag_detect.h"

#ifdef HAVE_APRILTAG

#include "common.h"

#include <apriltag_pose.h>
#include <tagStandard52h13.h>
#include <tagStandard41h12.h>
#include <common/image_u8.h>

#include <math.h>
#include <stdio.h>

AgApriltagContext *
ag_apriltag_create (void)
{
    AgApriltagContext *ctx = g_malloc0 (sizeof *ctx);

    ctx->family_52h13 = tagStandard52h13_create ();
    ctx->family_41h12 = tagStandard41h12_create ();
    ctx->detector     = apriltag_detector_create ();

    apriltag_detector_add_family (ctx->detector, ctx->family_52h13);
    apriltag_detector_add_family (ctx->detector, ctx->family_41h12);

    ctx->detector->quad_decimate    = 1.5f;
    ctx->detector->quad_sigma       = 0.0f;
    ctx->detector->nthreads         = 1;
    ctx->detector->refine_edges     = 1;
    ctx->detector->decode_sharpening = 0.25;

    return ctx;
}

void
ag_apriltag_destroy (AgApriltagContext *ctx)
{
    if (!ctx)
        return;
    if (ctx->detector)
        apriltag_detector_destroy (ctx->detector);
    if (ctx->family_52h13)
        tagStandard52h13_destroy (ctx->family_52h13);
    if (ctx->family_41h12)
        tagStandard41h12_destroy (ctx->family_41h12);
    g_free (ctx);
}

void
ag_apriltag_estimate_intrinsics (guint proc_sub_w, guint proc_h,
                                 double *fx, double *fy,
                                 double *cx, double *cy)
{
    double total_bin = (double) (AG_SENSOR_WIDTH / 2) / (double) proc_sub_w;
    *fx = AG_LENS_FL_UM / (AG_PIXEL_PITCH_UM * total_bin);
    *fy = *fx;
    *cx = (double) proc_sub_w / 2.0;
    *cy = (double) proc_h / 2.0;
}

int
ag_detect_tags_and_pose (apriltag_detector_t *detector,
                         const guint8 *gray,
                         guint width, guint height,
                         double tag_size_m,
                         double fx, double fy, double cx, double cy,
                         guint64 frame_num, const char *eye_label,
                         AgTagOverlay *overlays, int max_overlays,
                         gboolean quiet)
{
    image_u8_t im = {
        .width  = (int32_t) width,
        .height = (int32_t) height,
        .stride = (int32_t) width,
        .buf    = (uint8_t *) gray
    };

    zarray_t *detections = apriltag_detector_detect (detector, &im);
    int n_overlays = 0;

    for (int i = 0; i < zarray_size (detections); i++) {
        apriltag_detection_t *det;
        zarray_get (detections, i, &det);

        apriltag_detection_info_t info = {
            .det     = det,
            .tagsize = tag_size_m,
            .fx      = fx,
            .fy      = fy,
            .cx      = cx,
            .cy      = cy
        };

        apriltag_pose_t pose;
        double pose_err = estimate_tag_pose (&info, &pose);

        if (!quiet) {
            printf ("apriltag frame=%" G_GUINT64_FORMAT
                    " eye=%s id=%d hamming=%d margin=%.1f"
                    " center=(%.1f,%.1f)"
                    " err=%.2e"
                    " R=[%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f]"
                    " t=[%.4f,%.4f,%.4f]\n",
                    frame_num, eye_label,
                    det->id, det->hamming, det->decision_margin,
                    det->c[0], det->c[1],
                    pose_err,
                    matd_get (pose.R, 0, 0), matd_get (pose.R, 0, 1),
                    matd_get (pose.R, 0, 2), matd_get (pose.R, 1, 0),
                    matd_get (pose.R, 1, 1), matd_get (pose.R, 1, 2),
                    matd_get (pose.R, 2, 0), matd_get (pose.R, 2, 1),
                    matd_get (pose.R, 2, 2),
                    matd_get (pose.t, 0, 0), matd_get (pose.t, 1, 0),
                    matd_get (pose.t, 2, 0));
        }

        /* Store corner points and metadata for overlay / tracker. */
        if (n_overlays < max_overlays) {
            for (int c = 0; c < 4; c++) {
                overlays[n_overlays].p[c][0] = det->p[c][0];
                overlays[n_overlays].p[c][1] = det->p[c][1];
            }
            overlays[n_overlays].id        = det->id;
            overlays[n_overlays].center[0] = det->c[0];
            overlays[n_overlays].center[1] = det->c[1];
            overlays[n_overlays].t[0]      = matd_get (pose.t, 0, 0);
            overlays[n_overlays].t[1]      = matd_get (pose.t, 1, 0);
            overlays[n_overlays].t[2]      = matd_get (pose.t, 2, 0);
            n_overlays++;
        }

        matd_destroy (pose.R);
        matd_destroy (pose.t);
    }

    apriltag_detections_destroy (detections);
    return n_overlays;
}

/* ------------------------------------------------------------------ */
/*  Stateful tag tracker                                                */
/* ------------------------------------------------------------------ */

AgTagTracker *
ag_tag_tracker_create (const char *eye_label,
                       double move_threshold_px,
                       double move_threshold_m,
                       int disappear_frames)
{
    AgTagTracker *tr = g_malloc0 (sizeof *tr);
    tr->eye_label         = eye_label;
    tr->n_tags            = 0;
    tr->move_threshold_px = move_threshold_px;
    tr->move_threshold_m  = move_threshold_m;
    tr->disappear_frames  = disappear_frames;
    return tr;
}

void
ag_tag_tracker_update (AgTagTracker *tracker,
                       const AgTagOverlay *overlays,
                       int n_overlays,
                       guint64 frame_num)
{
    for (int i = 0; i < n_overlays; i++) {
        const AgTagOverlay *ov = &overlays[i];

        /* Search for existing tracked tag with this ID. */
        AgTrackedTag *found = NULL;
        for (int j = 0; j < tracker->n_tags; j++) {
            if (tracker->tags[j].id == ov->id) {
                found = &tracker->tags[j];
                break;
            }
        }

        if (!found) {
            /* Brand new tag — add to tracker. */
            if (tracker->n_tags < AG_MAX_TRACKED_TAGS) {
                found = &tracker->tags[tracker->n_tags++];
                found->id = ov->id;
                found->active = FALSE;  /* will be set below */
            } else {
                continue;  /* tracker full */
            }
        }

        if (!found->active) {
            /* Appeared (first time or reappeared after disappearance). */
            printf ("apriltag_event eye=%s id=%d event=appeared"
                    " frame=%" G_GUINT64_FORMAT
                    " center=(%.1f,%.1f) t=[%.4f,%.4f,%.4f]\n",
                    tracker->eye_label, ov->id, frame_num,
                    ov->center[0], ov->center[1],
                    ov->t[0], ov->t[1], ov->t[2]);
            found->active = TRUE;
        } else {
            /* Check if it moved significantly. */
            double dcx = ov->center[0] - found->center_x;
            double dcy = ov->center[1] - found->center_y;
            double dist_px = sqrt (dcx * dcx + dcy * dcy);

            double dtx = ov->t[0] - found->tx;
            double dty = ov->t[1] - found->ty;
            double dtz = ov->t[2] - found->tz;
            double dist_m = sqrt (dtx * dtx + dty * dty + dtz * dtz);

            if (dist_px > tracker->move_threshold_px ||
                dist_m  > tracker->move_threshold_m) {
                printf ("apriltag_event eye=%s id=%d event=moved"
                        " frame=%" G_GUINT64_FORMAT
                        " center=(%.1f,%.1f) t=[%.4f,%.4f,%.4f]\n",
                        tracker->eye_label, ov->id, frame_num,
                        ov->center[0], ov->center[1],
                        ov->t[0], ov->t[1], ov->t[2]);
            }
        }

        found->center_x       = ov->center[0];
        found->center_y       = ov->center[1];
        found->tx             = ov->t[0];
        found->ty             = ov->t[1];
        found->tz             = ov->t[2];
        found->frame_last_seen = frame_num;
    }
}

void
ag_tag_tracker_check_disappeared (AgTagTracker *tracker,
                                   guint64 frame_num)
{
    for (int i = 0; i < tracker->n_tags; i++) {
        AgTrackedTag *tag = &tracker->tags[i];
        if (tag->active &&
            frame_num - tag->frame_last_seen > (guint64) tracker->disappear_frames) {
            printf ("apriltag_event eye=%s id=%d event=disappeared"
                    " frame=%" G_GUINT64_FORMAT
                    " last_center=(%.1f,%.1f)\n",
                    tracker->eye_label, tag->id, frame_num,
                    tag->center_x, tag->center_y);
            tag->active = FALSE;
        }
    }
}

void
ag_tag_tracker_destroy (AgTagTracker *tracker)
{
    g_free (tracker);
}

#endif /* HAVE_APRILTAG */
