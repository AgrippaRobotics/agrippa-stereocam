/*
 * agrippa.h — public C API for libagrippa
 *
 * Exposes the same camera open/configure/capture pipeline used by
 * ag-cam-tools, but as a reusable shared library with a persistent
 * camera handle.  Each capture returns Bayer planes (split from
 * DualBayerRG8 for stereo Lucid heads, or the single sensor plane for
 * monocular Lucid cameras) without going through disk.
 *
 * Memory model:
 *   - ag_camera_open() returns an opaque handle owned by the caller.
 *   - ag_camera_capture() points AgFrame.left / AgFrame.right at
 *     library-owned scratch buffers that remain valid until the next
 *     ag_camera_capture() or ag_camera_close().  The caller should
 *     copy the data if it needs to outlive the next capture.
 *   - ag_camera_release_frame() is a no-op in this revision but is
 *     part of the API so callers can opt into a zero-copy buffer
 *     hand-off in the future without an ABI break.
 *
 * Thread safety: an AgCamera handle is not internally synchronized.
 * Callers must serialize access to a given handle.
 */

#ifndef AGRIPPA_H
#define AGRIPPA_H

#include "remap.h"   /* for AgRemapTable, exposed via ag_camera_get_remap_* */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AgCamera AgCamera;

typedef struct {
    /* Camera selection.  At most one of address / serial may be set;
     * pass NULL for unused fields.  interface_name forces a NIC. */
    const char *address;
    const char *serial;
    const char *interface_name;

    /* Exposure / gain.  Pass exposure_us <= 0 or gain_db < 0 to leave
     * the camera default in place.  auto_expose=1 overrides both and
     * runs an auto-expose-and-lock pass at open time. */
    double      exposure_us;
    double      gain_db;
    int         auto_expose;

    /* Sensor binning (1 or 2) and GigE packet size (0 = auto). */
    int         binning;
    int         packet_size;

    /* Optional rectification: either a local calibration session path
     * or an on-camera slot index (0-2).  Set calibration_local_path to
     * NULL and calibration_slot to -1 to skip. */
    const char *calibration_local_path;
    int         calibration_slot;

    /* Acquisition mode.  continuous=1 (recommended for streaming)
     * starts continuous acquisition once at open time so subsequent
     * captures pay only the per-frame software-trigger cost.
     * continuous=0 mimics ag-cam-tools' SingleFrame behaviour and
     * restarts acquisition for every capture. */
    int         continuous;

    /* Verbose diagnostic prints during configuration. */
    int         verbose;
} AgOpenParams;

typedef struct {
    /* Raw Bayer planes for the captured frame.
     *
     * For stereo cameras (DualBayerRG8): left and right each point at
     * a width*height single-eye plane, where width = sub-eye width
     * after software binning and height = full frame height after
     * software binning.
     *
     * For mono cameras (BayerRG8/Mono8): left points at the full
     * width*height frame and right is NULL.
     *
     * Buffers are library-owned and remain valid until the next call
     * to ag_camera_capture() or ag_camera_close(). */
    unsigned char *left;
    unsigned char *right;

    /* Per-eye buffer dimensions (post-binning). */
    unsigned int   width;
    unsigned int   height;

    /* Camera-reported frame id and timestamp in nanoseconds. */
    unsigned long  frame_id;
    unsigned long  timestamp_ns;
} AgFrame;

/*
 * Open a camera and prepare it for capture.  Returns NULL on failure;
 * call ag_camera_last_error() on a partially-initialised handle is
 * not supported, so callers should rely on stderr diagnostics in that
 * case.
 */
AgCamera *ag_camera_open(const AgOpenParams *params);

/*
 * Capture a single frame.  Returns 0 on success and fills *out;
 * returns -1 on failure and stores a diagnostic string accessible
 * via ag_camera_last_error().  *out is only valid on success.
 */
int ag_camera_capture(AgCamera *cam, AgFrame *out);

/*
 * Release a frame returned by ag_camera_capture().  In this revision
 * the buffers are shared scratch space inside the AgCamera handle, so
 * this call simply zeroes out *frame; the underlying memory is reused
 * by the next capture.  Provided so future zero-copy implementations
 * can be wired in without an ABI break.
 */
void ag_camera_release_frame(AgCamera *cam, AgFrame *frame);

/*
 * Stop acquisition (if running), free rectification tables, and tear
 * down the camera handle.  Does not call arv_shutdown(), so further
 * ag_camera_open() calls in the same process keep their discovery
 * cache warm.
 */
void ag_camera_close(AgCamera *cam);

/*
 * Last error message captured on this handle, or "" if none.  The
 * returned pointer is owned by the handle and remains valid until the
 * next library call on the same handle.
 */
const char *ag_camera_last_error(AgCamera *cam);

/*
 * Sensor topology accessor: returns 1 for a dual-eye DualBayerRG8
 * Lucid head, 0 for a monocular BayerRG8 / Mono8 camera.
 */
int ag_camera_is_stereo(const AgCamera *cam);

/*
 * Returns 1 if the captured planes still hold a valid Bayer mosaic
 * (apply debayer to produce color), or 0 if the data should be treated
 * as grayscale (e.g. when 2x2 averaging during binning destroyed the
 * CFA pattern).  Constant after ag_camera_open() succeeds.
 */
int ag_camera_data_is_bayer(const AgCamera *cam);

/*
 * Borrowed pointers to the rectification remap tables loaded when
 * calibration_local_path or calibration_slot was set in AgOpenParams.
 * Return NULL if no calibration was requested.  Memory is owned by the
 * AgCamera handle and freed by ag_camera_close().
 */
const AgRemapTable *ag_camera_get_remap_left(const AgCamera *cam);
const AgRemapTable *ag_camera_get_remap_right(const AgCamera *cam);

#ifdef __cplusplus
}
#endif

#endif /* AGRIPPA_H */
