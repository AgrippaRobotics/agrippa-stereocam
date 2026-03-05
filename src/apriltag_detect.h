/*
 * apriltag_detect.h — shared AprilTag detection for ag-cam-tools
 *
 * Provides detector lifecycle helpers and the per-frame detection
 * function used by both `stream` and `capture` subcommands.
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

/* Corners of one detected tag (pixel coords within a single eye). */
typedef struct {
    double p[4][2];   /* corner points, counter-clockwise */
} AgTagOverlay;

#define AG_MAX_TAG_OVERLAYS  32

/*
 * Create a standard AprilTag detector (tagStandard52h13, quad_decimate=1.5,
 * nthreads=1, refine_edges=1, decode_sharpening=0.25).
 *
 * Sets *out_family to the allocated tag family (caller must destroy both).
 * Returns the detector, or NULL on failure.
 */
apriltag_detector_t *ag_apriltag_detector_create (apriltag_family_t **out_family);

/*
 * Destroy a detector/family pair returned by ag_apriltag_detector_create().
 * Either or both may be NULL.
 */
void ag_apriltag_detector_destroy (apriltag_detector_t *detector,
                                   apriltag_family_t *family);

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
 * Prints one structured line to stdout per detection:
 *   apriltag frame=N eye=<label> id=N hamming=N margin=N center=(...) ...
 *
 * Returns the number of tags detected (and stored in overlays[]).
 */
int ag_detect_tags_and_pose (apriltag_detector_t *detector,
                             const guint8 *gray,
                             guint width, guint height,
                             double tag_size_m,
                             double fx, double fy, double cx, double cy,
                             guint64 frame_num, const char *eye_label,
                             AgTagOverlay *overlays, int max_overlays);

#endif /* HAVE_APRILTAG */
#endif /* AG_APRILTAG_DETECT_H */
