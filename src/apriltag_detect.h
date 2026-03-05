/*
 * apriltag_detect.h — shared AprilTag detection for ag-cam-tools
 *
 * Provides detector lifecycle helpers, per-frame detection, and
 * stateful tag tracking used by `stream`, `capture`, and `burst`.
 *
 * All declarations are compiled out when HAVE_APRILTAG is not defined,
 * so callers may include this header unconditionally.
 */

#ifndef AG_APRILTAG_DETECT_H
#define AG_APRILTAG_DETECT_H

#include <glib.h>

#ifdef HAVE_APRILTAG

#include <apriltag.h>

/* IMX273 sensor: 3.45 µm pixel pitch, 3 mm lens. */
#define AG_PIXEL_PITCH_UM  3.45
#define AG_LENS_FL_UM      3000.0  /* 3 mm */

/* Corners and metadata for one detected tag (pixel coords within a single eye). */
typedef struct {
    double p[4][2];   /* corner points, counter-clockwise */
    int    id;        /* tag family ID */
    double center[2]; /* center pixel coordinates */
    double t[3];      /* translation vector from pose estimate */
} AgTagOverlay;

#define AG_MAX_TAG_OVERLAYS  32

/*
 * Bundled AprilTag detector context — owns the detector and all
 * registered tag families.
 */
typedef struct {
    apriltag_detector_t *detector;
    apriltag_family_t   *family_52h13;
    apriltag_family_t   *family_41h12;
} AgApriltagContext;

/*
 * Create an AprilTag detector with tagStandard52h13 and tagStandard41h12
 * families (quad_decimate=1.5, nthreads=1, refine_edges=1,
 * decode_sharpening=0.25).
 *
 * Returns the context, or NULL on failure.
 */
AgApriltagContext *ag_apriltag_create (void);

/*
 * Destroy a context returned by ag_apriltag_create().  May be NULL.
 */
void ag_apriltag_destroy (AgApriltagContext *ctx);

/*
 * Compute estimated camera intrinsics from frame dimensions using
 * the known IMX273 sensor geometry.
 *
 * proc_sub_w / proc_h are the per-eye processed dimensions.
 */
void ag_apriltag_estimate_intrinsics (guint proc_sub_w, guint proc_h,
                                      double *fx, double *fy,
                                      double *cx, double *cy);

/*
 * Detect AprilTags in a single grayscale eye image and estimate pose.
 *
 * When quiet is FALSE, prints one structured line to stdout per
 * detection.  When quiet is TRUE, output is suppressed (use the
 * tracker for event-based output instead).
 *
 * Returns the number of tags detected (and stored in overlays[]).
 */
int ag_detect_tags_and_pose (apriltag_detector_t *detector,
                             const guint8 *gray,
                             guint width, guint height,
                             double tag_size_m,
                             double fx, double fy, double cx, double cy,
                             guint64 frame_num, const char *eye_label,
                             AgTagOverlay *overlays, int max_overlays,
                             gboolean quiet);

/* ------------------------------------------------------------------ */
/*  Stateful tag tracker (stream mode only)                            */
/* ------------------------------------------------------------------ */

#define AG_MAX_TRACKED_TAGS  64

typedef struct {
    int      id;               /* tag family ID */
    double   center_x;         /* last-seen center X (pixels) */
    double   center_y;         /* last-seen center Y (pixels) */
    double   tx, ty, tz;       /* last-seen translation vector */
    guint64  frame_last_seen;  /* frame number when last detected */
    gboolean active;           /* currently tracked? */
} AgTrackedTag;

typedef struct {
    const char   *eye_label;
    AgTrackedTag  tags[AG_MAX_TRACKED_TAGS];
    int           n_tags;
    double        move_threshold_px;  /* center pixel displacement */
    double        move_threshold_m;   /* translation displacement */
    int           disappear_frames;   /* frames absent before event */
} AgTagTracker;

AgTagTracker *ag_tag_tracker_create (const char *eye_label,
                                     double move_threshold_px,
                                     double move_threshold_m,
                                     int disappear_frames);

/*
 * Update tracker with current frame's detections.
 * Prints "appeared" and "moved" events to stdout.
 */
void ag_tag_tracker_update (AgTagTracker *tracker,
                            const AgTagOverlay *overlays,
                            int n_overlays,
                            guint64 frame_num);

/*
 * Check for tags that have disappeared (not seen for disappear_frames).
 * Call once per frame after update.
 */
void ag_tag_tracker_check_disappeared (AgTagTracker *tracker,
                                       guint64 frame_num);

void ag_tag_tracker_destroy (AgTagTracker *tracker);

#endif /* HAVE_APRILTAG */
#endif /* AG_APRILTAG_DETECT_H */
