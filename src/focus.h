/*
 * focus.h — Variance of Laplacian focus metric (pure C99)
 */

#ifndef AG_FOCUS_H
#define AG_FOCUS_H

#include <stdint.h>

/*
 * Compute a focus sharpness score for an 8-bit grayscale image.
 *
 * Applies a 3×3 Laplacian kernel and returns the variance of the
 * response over the specified ROI.  Higher values indicate sharper images.
 *
 * The ROI is clamped inward by 1 pixel on each side to provide margin
 * for the kernel.  If the resulting region has fewer than 2 pixels in
 * either dimension, returns 0.0.
 */
double compute_focus_score (const uint8_t *image, int width, int height,
                            int roi_x, int roi_y, int roi_w, int roi_h);

#endif /* AG_FOCUS_H */
