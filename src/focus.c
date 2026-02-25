/*
 * focus.c — Variance of Laplacian focus metric (pure C99)
 *
 * Computes image sharpness as the variance of a 3×3 Laplacian response:
 *
 *   Kernel:   0  -1   0
 *            -1   4  -1
 *             0  -1   0
 *
 *   Score = E[L²] - (E[L])²
 *
 * Uses integer math in the inner loop with 64-bit accumulators;
 * floating-point only for the final variance computation.
 */

#include "focus.h"

double
compute_focus_score (const uint8_t *image, int width, int height,
                     int roi_x, int roi_y, int roi_w, int roi_h)
{
    /* Clamp ROI to valid range (1-pixel border for kernel). */
    int x0 = roi_x < 1 ? 1 : roi_x;
    int y0 = roi_y < 1 ? 1 : roi_y;
    int x1 = roi_x + roi_w;
    int y1 = roi_y + roi_h;

    if (x1 > width  - 1) x1 = width  - 1;
    if (y1 > height - 1) y1 = height - 1;

    if (x1 - x0 < 2 || y1 - y0 < 2)
        return 0.0;

    int64_t sum    = 0;
    int64_t sum_sq = 0;
    int64_t count  = 0;

    for (int y = y0; y < y1; y++) {
        const uint8_t *row_prev = image + (y - 1) * width;
        const uint8_t *row_curr = image +  y      * width;
        const uint8_t *row_next = image + (y + 1) * width;

        for (int x = x0; x < x1; x++) {
            int lap = 4 * (int) row_curr[x]
                        - (int) row_curr[x - 1]
                        - (int) row_curr[x + 1]
                        - (int) row_prev[x]
                        - (int) row_next[x];

            sum    += lap;
            sum_sq += (int64_t) lap * lap;
            count++;
        }
    }

    if (count == 0)
        return 0.0;

    double mean     = (double) sum / (double) count;
    double mean_sq  = (double) sum_sq / (double) count;

    return mean_sq - mean * mean;
}
