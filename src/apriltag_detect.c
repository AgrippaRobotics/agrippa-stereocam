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
#include <common/image_u8.h>

#include <stdio.h>

apriltag_detector_t *
ag_apriltag_detector_create (apriltag_family_t **out_family)
{
    apriltag_family_t   *family   = tagStandard52h13_create ();
    apriltag_detector_t *detector = apriltag_detector_create ();

    apriltag_detector_add_family (detector, family);

    detector->quad_decimate    = 1.5f;
    detector->quad_sigma       = 0.0f;
    detector->nthreads         = 1;
    detector->refine_edges     = 1;
    detector->decode_sharpening = 0.25;

    *out_family = family;
    return detector;
}

void
ag_apriltag_detector_destroy (apriltag_detector_t *detector,
                              apriltag_family_t *family)
{
    if (detector)
        apriltag_detector_destroy (detector);
    if (family)
        tagStandard52h13_destroy (family);
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
                         AgTagOverlay *overlays, int max_overlays)
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

        /* Store corner points for overlay rendering. */
        if (n_overlays < max_overlays) {
            for (int c = 0; c < 4; c++) {
                overlays[n_overlays].p[c][0] = det->p[c][0];
                overlays[n_overlays].p[c][1] = det->p[c][1];
            }
            n_overlays++;
        }

        matd_destroy (pose.R);
        matd_destroy (pose.t);
    }

    apriltag_detections_destroy (detections);
    return n_overlays;
}

#endif /* HAVE_APRILTAG */
