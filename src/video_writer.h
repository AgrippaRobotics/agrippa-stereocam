/*
 * video_writer.h — MP4 video recording for ag-cam-tools
 *
 * Encodes RGB24 stereo frame pairs to H.264 MP4 files.
 * Requires FFmpeg (libavcodec, libavformat, libswscale).
 */

#ifndef AG_VIDEO_WRITER_H
#define AG_VIDEO_WRITER_H

#include <stdint.h>

typedef enum {
    AG_VIDEO_LAYOUT_SBS,        /* side-by-side: width = eye_w * 2 */
    AG_VIDEO_LAYOUT_TOP_BOTTOM, /* over-under:   height = eye_h * 2 */
    AG_VIDEO_LAYOUT_SEPARATE,   /* two files: _left.mp4 + _right.mp4 */
    AG_VIDEO_LAYOUT_MONO,       /* single-sensor camera: no pair */
} AgVideoLayout;

typedef struct AgVideoWriter AgVideoWriter;

/*
 * Parse layout string: "sbs", "tb" / "top-bottom", "separate".
 * Returns 0 on success, -1 on unrecognised string.
 */
int ag_video_layout_parse (const char *str, AgVideoLayout *out);

/*
 * Create a video writer.
 *
 * path:   output MP4 file path.  For SEPARATE layout, the base name
 *         is used to generate _left.mp4 and _right.mp4 variants.
 * eye_w:  per-eye frame width in pixels.
 * eye_h:  per-eye frame height in pixels.
 * fps:    target frame rate.
 * layout: stereo layout mode.
 *
 * Returns NULL on failure (prints its own diagnostic).
 */
AgVideoWriter *ag_video_writer_new (const char *path, int eye_w, int eye_h,
                                     double fps, AgVideoLayout layout);

/*
 * Feed a stereo frame pair.
 *
 * left_rgb and right_rgb are RGB24 buffers (eye_w * eye_h * 3 bytes each).
 * The writer composites them according to layout, converts to YUV420P,
 * encodes, and muxes.
 *
 * Returns 0 on success, -1 on error.  Returns -1 if called on a writer
 * created with AG_VIDEO_LAYOUT_MONO.
 */
int ag_video_writer_add_frame (AgVideoWriter *w,
                                const uint8_t *left_rgb,
                                const uint8_t *right_rgb);

/*
 * Feed a single (mono) frame.  rgb is an RGB24 buffer of
 * eye_w * eye_h * 3 bytes.  Returns 0 on success, -1 on error.
 * Returns -1 if called on a writer not created with AG_VIDEO_LAYOUT_MONO.
 */
int ag_video_writer_add_mono_frame (AgVideoWriter *w, const uint8_t *rgb);

/*
 * Flush encoder, write MP4 trailer, close file(s), free resources.
 * Safe to call with NULL (no-op).
 * Returns the number of frames written, or -1 on error during flush.
 */
int64_t ag_video_writer_close (AgVideoWriter *w);

#endif /* AG_VIDEO_WRITER_H */
