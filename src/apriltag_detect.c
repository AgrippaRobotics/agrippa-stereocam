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
#include <tag16h5.h>
#include <tag25h9.h>
#include <tag36h10.h>
#include <tag36h11.h>
#include <tagCircle21h7.h>
#include <tagCircle49h12.h>
#include <tagCustom48h12.h>
#include <tagStandard41h12.h>
#include <tagStandard52h13.h>
#include <common/image_u8.h>

#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/*  Family table — every family apriltag 3 ships                        */
/* ------------------------------------------------------------------ */

typedef apriltag_family_t *(*AgFamilyCreate) (void);
typedef void (*AgFamilyDestroy) (apriltag_family_t *);

typedef struct {
    const char      *name;
    AgFamilyCreate   create;
    AgFamilyDestroy  destroy;
    int              ncodes;   /* for the cost warning only */
} AgFamilyDef;

/*
 * tag36h11 with a TWO-CELL black border, as calib.io and Kalibr render AprilGrid
 * calibration targets.
 *
 * apriltag3's stock tag36h11 hard-codes a one-cell border (width_at_border 8,
 * total_width 10), so on a two-cell board its bit sample points land on the border
 * ring instead of the data and every quad fails to decode -- not poorly, but zero
 * tags at any distance, exposure or image quality. Widening the frame by one cell on
 * each side and shifting every bit coordinate to match moves the samples back onto
 * the payload. Same 587 codes, same hamming distances; only the geometry differs.
 *
 * Measured on a UR5e bench cell, PDH016S at binning 1, calib.io 7x10 / 25 mm board:
 * stock tag36h11 reads 0 of 70 tags, this family reads 57-69 depending on exposure,
 * and beats OpenCV ArUco with markerBorderBits=2 on every frame tested.
 *
 * tag36h11_destroy is the correct destructor: it frees name, bit_x, bit_y and the
 * struct, all of which this still owns.
 */
static apriltag_family_t *
tag36h11b2_create (void)
{
    apriltag_family_t *tf = tag36h11_create ();

    tf->width_at_border += 2;
    tf->total_width     += 2;
    for (uint32_t i = 0; i < tf->nbits; i++) {
        tf->bit_x[i] += 1;
        tf->bit_y[i] += 1;
    }

    free (tf->name);
    tf->name = strdup ("tag36h11b2");
    return tf;
}

static const AgFamilyDef ag_families[AG_TAG_FAMILY_MAX] = {
    { "tag16h5",          tag16h5_create,          tag16h5_destroy,             30 },
    { "tag25h9",          tag25h9_create,          tag25h9_destroy,             35 },
    { "tag36h10",         tag36h10_create,         tag36h10_destroy,          2320 },
    { "tag36h11",         tag36h11_create,         tag36h11_destroy,           587 },
    { "tag36h11b2",       tag36h11b2_create,       tag36h11_destroy,           587 },
    { "tagCircle21h7",    tagCircle21h7_create,    tagCircle21h7_destroy,       38 },
    { "tagCircle49h12",   tagCircle49h12_create,   tagCircle49h12_destroy,   65535 },
    { "tagCustom48h12",   tagCustom48h12_create,   tagCustom48h12_destroy,   42211 },
    { "tagStandard41h12", tagStandard41h12_create, tagStandard41h12_destroy,  2115 },
    { "tagStandard52h13", tagStandard52h13_create, tagStandard52h13_destroy, 48714 },
};

/* Registered when nobody says otherwise — the historical pair, so an empty
 * registry behaves exactly as it always did. */
static const char ag_default_families[] = "tagStandard52h13,tagStandard41h12";

/* Big enough that registering it needlessly is a real per-frame cost. */
#define AG_FAMILY_EXPENSIVE_NCODES  10000

const char *const *
ag_apriltag_family_names (int *n_out)
{
    static const char *names[AG_TAG_FAMILY_MAX];
    static gboolean init = FALSE;
    if (!init) {
        for (int i = 0; i < AG_TAG_FAMILY_MAX; i++)
            names[i] = ag_families[i].name;
        init = TRUE;
    }
    if (n_out)
        *n_out = AG_TAG_FAMILY_MAX;
    return names;
}

int
ag_apriltag_family_index (const char *token)
{
    if (!token || !*token)
        return -1;

    for (int i = 0; i < AG_TAG_FAMILY_MAX; i++)
        if (g_ascii_strcasecmp (token, ag_families[i].name) == 0)
            return i;

    /* Legacy bare suffixes ("41h12", "52h13"). Only an UNAMBIGUOUS suffix
     * match counts, so a family added later can never silently steal a token
     * that used to mean something else. */
    int hit = -1;
    for (int i = 0; i < AG_TAG_FAMILY_MAX; i++) {
        size_t nl = strlen (ag_families[i].name), tl = strlen (token);
        if (tl < nl && g_ascii_strcasecmp (ag_families[i].name + (nl - tl), token) == 0) {
            if (hit >= 0)
                return -1;
            hit = i;
        }
    }
    return hit;
}

char *
ag_apriltag_family_summary (const AgApriltagContext *ctx)
{
    GString *s = g_string_new (NULL);
    if (ctx) {
        for (int i = 0; i < AG_TAG_FAMILY_MAX; i++)
            if (ctx->families[i])
                g_string_append_printf (s, "%s%s", s->len ? "," : "", ag_families[i].name);
    }
    if (!s->len)
        g_string_append (s, "(none)");
    return g_string_free (s, FALSE);
}

AgApriltagContext *
ag_apriltag_create (void)
{
    return ag_apriltag_create_families (NULL);
}

AgApriltagContext *
ag_apriltag_create_families (const char *csv)
{
    AgApriltagContext *ctx = g_malloc0 (sizeof *ctx);
    gboolean want[AG_TAG_FAMILY_MAX] = { FALSE };
    int n_want = 0;

    if (csv && *csv) {
        char **toks = g_strsplit (csv, ",", -1);
        for (int i = 0; toks[i]; i++) {
            char *tok = g_strstrip (toks[i]);
            if (!*tok)
                continue;
            int idx = ag_apriltag_family_index (tok);
            if (idx < 0) {
                fprintf (stderr, "apriltag: unknown family '%s' — ignored\n", tok);
                continue;
            }
            if (!want[idx]) {
                want[idx] = TRUE;
                n_want++;
            }
        }
        g_strfreev (toks);
    }

    if (n_want == 0) {
        /* Nothing asked for, or nothing recognised: fall back to the default
         * pair rather than leaving the detector blind. */
        if (csv && *csv)
            fprintf (stderr, "apriltag: no usable family in '%s' — using defaults\n", csv);
        char **toks = g_strsplit (ag_default_families, ",", -1);
        for (int i = 0; toks[i]; i++) {
            int idx = ag_apriltag_family_index (toks[i]);
            if (idx >= 0)
                want[idx] = TRUE;
        }
        g_strfreev (toks);
    }

    ctx->detector = apriltag_detector_create ();
    for (int i = 0; i < AG_TAG_FAMILY_MAX; i++) {
        if (!want[i])
            continue;
        ctx->families[i] = ag_families[i].create ();
        apriltag_detector_add_family (ctx->detector, ctx->families[i]);
        if (ag_families[i].ncodes >= AG_FAMILY_EXPENSIVE_NCODES)
            fprintf (stderr, "apriltag: %s registered (%d codes — decoded against on"
                             " every quad)\n",
                     ag_families[i].name, ag_families[i].ncodes);
    }

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
    for (int i = 0; i < AG_TAG_FAMILY_MAX; i++)
        if (ctx->families[i])
            ag_families[i].destroy (ctx->families[i]);
    g_free (ctx);
}

/*
 * Family name for a detection. det->family is owned by the context, which
 * outlives every overlay built from it, so the overlay borrows the pointer.
 */
static const char *
overlay_family (const apriltag_detection_t *det)
{
    return (det && det->family && det->family->name) ? det->family->name : "?";
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

zarray_t *
ag_detect_tags (apriltag_detector_t *detector,
                const guint8 *gray, guint width, guint height)
{
    image_u8_t im = {
        .width  = (int32_t) width,
        .height = (int32_t) height,
        .stride = (int32_t) width,
        .buf    = (uint8_t *) gray
    };
    return apriltag_detector_detect (detector, &im);
}

int
ag_pose_from_detections (zarray_t *detections,
                         double tag_size_m,
                         double fx, double fy, double cx, double cy,
                         AgTagOverlay *overlays, int max_overlays)
{
    int n_overlays = 0;

    for (int i = 0; i < zarray_size (detections) && n_overlays < max_overlays; i++) {
        apriltag_detection_t *det;
        zarray_get (detections, i, &det);

        apriltag_detection_info_t info = {
            .det = det, .tagsize = tag_size_m,
            .fx = fx, .fy = fy, .cx = cx, .cy = cy
        };
        apriltag_pose_t pose;
        estimate_tag_pose (&info, &pose);

        for (int p = 0; p < 4; p++) {
            overlays[n_overlays].p[p][0] = det->p[p][0];
            overlays[n_overlays].p[p][1] = det->p[p][1];
        }
        overlays[n_overlays].id        = det->id;
        overlays[n_overlays].family    = overlay_family (det);
        overlays[n_overlays].center[0] = det->c[0];
        overlays[n_overlays].center[1] = det->c[1];
        overlays[n_overlays].t[0]      = matd_get (pose.t, 0, 0);
        overlays[n_overlays].t[1]      = matd_get (pose.t, 1, 0);
        overlays[n_overlays].t[2]      = matd_get (pose.t, 2, 0);
        for (int r = 0; r < 3; r++)
            for (int cc = 0; cc < 3; cc++)
                overlays[n_overlays].R[r * 3 + cc] = matd_get (pose.R, r, cc);
        n_overlays++;

        matd_destroy (pose.R);
        matd_destroy (pose.t);
    }
    return n_overlays;
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
                    " eye=%s family=%s id=%d hamming=%d margin=%.1f"
                    " center=(%.1f,%.1f)"
                    " err=%.2e"
                    " R=[%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f]"
                    " t=[%.4f,%.4f,%.4f]\n",
                    frame_num, eye_label, overlay_family (det),
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
            overlays[n_overlays].family    = overlay_family (det);
            overlays[n_overlays].center[0] = det->c[0];
            overlays[n_overlays].center[1] = det->c[1];
            overlays[n_overlays].t[0]      = matd_get (pose.t, 0, 0);
            overlays[n_overlays].t[1]      = matd_get (pose.t, 1, 0);
            overlays[n_overlays].t[2]      = matd_get (pose.t, 2, 0);
            for (int r = 0; r < 3; r++)
                for (int c = 0; c < 3; c++)
                    overlays[n_overlays].R[r * 3 + c] = matd_get (pose.R, r, c);
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

        /* Search by (family, id) — an id is unique only within its family, so
         * keying on the id alone merges two different physical tags. */
        AgTrackedTag *found = NULL;
        for (int j = 0; j < tracker->n_tags; j++) {
            if (tracker->tags[j].id == ov->id &&
                g_strcmp0 (tracker->tags[j].family, ov->family) == 0) {
                found = &tracker->tags[j];
                break;
            }
        }

        if (!found) {
            /* Brand new tag — add to tracker. */
            if (tracker->n_tags < AG_MAX_TRACKED_TAGS) {
                found = &tracker->tags[tracker->n_tags++];
                found->id = ov->id;
                found->family = ov->family;
                found->active = FALSE;  /* will be set below */
            } else {
                continue;  /* tracker full */
            }
        }

        if (!found->active) {
            /* Appeared (first time or reappeared after disappearance). */
            printf ("apriltag_event eye=%s family=%s id=%d event=appeared"
                    " frame=%" G_GUINT64_FORMAT
                    " center=(%.1f,%.1f) t=[%.4f,%.4f,%.4f]\n",
                    tracker->eye_label, ov->family, ov->id, frame_num,
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
                printf ("apriltag_event eye=%s family=%s id=%d event=moved"
                        " frame=%" G_GUINT64_FORMAT
                        " center=(%.1f,%.1f) t=[%.4f,%.4f,%.4f]\n",
                        tracker->eye_label, ov->family, ov->id, frame_num,
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
            printf ("apriltag_event eye=%s family=%s id=%d event=disappeared"
                    " frame=%" G_GUINT64_FORMAT
                    " last_center=(%.1f,%.1f)\n",
                    tracker->eye_label, tag->family, tag->id, frame_num,
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
