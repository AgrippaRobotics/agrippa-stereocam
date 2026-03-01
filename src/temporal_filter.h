/*
 * temporal_filter.h — temporal median filter for disparity maps
 *
 * Maintains a ring buffer of N consecutive disparity frames and computes
 * the per-pixel temporal median.  This suppresses frame-to-frame noise
 * (salt-and-pepper, random SGBM failures) while preserving consistent
 * depth structure.
 *
 * A simple scene-change detector resets the buffer when a large fraction
 * of pixels change significantly, avoiding stale data after sudden
 * camera or scene motion.
 */

#ifndef AG_TEMPORAL_FILTER_H
#define AG_TEMPORAL_FILTER_H

#include <stdint.h>

/* Opaque context. */
typedef struct AgTemporalFilter AgTemporalFilter;

/*
 * Create a temporal median filter context.
 *
 * width, height:  per-eye image dimensions (pixels).
 * depth:          number of frames in the ring buffer (3–9 recommended).
 *                 Must be >= 2.  Odd values give a true median; even
 *                 values average the two middle samples.
 *
 * Returns NULL on error.
 */
AgTemporalFilter *ag_temporal_filter_create (uint32_t width, uint32_t height,
                                              int depth);

/*
 * Push a new disparity frame and retrieve the temporal median.
 *
 * disparity_in:   the latest disparity map (width*height int16_t, Q4.4).
 * disparity_out:  receives the temporal median (width*height int16_t).
 *                 May alias disparity_in for in-place operation.
 *
 * Returns 0 on success, -1 on error.
 * Until the buffer is full (first `depth` frames), the output is a
 * median over however many frames have been pushed so far.
 */
int ag_temporal_filter_push (AgTemporalFilter *ctx,
                              const int16_t *disparity_in,
                              int16_t *disparity_out);

/*
 * Reset the ring buffer (e.g. after a scene change or parameter change).
 * The next push will start accumulating from scratch.
 */
void ag_temporal_filter_reset (AgTemporalFilter *ctx);

/* Destroy context and free all resources.  NULL-safe. */
void ag_temporal_filter_destroy (AgTemporalFilter *ctx);

#endif /* AG_TEMPORAL_FILTER_H */
