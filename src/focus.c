/*
 * focus.c — Focus metrics for lens adjustment (pure C99)
 *
 * Metrics:
 *
 *   Laplacian  — Variance of the 3x3 Laplacian response.
 *                Kernel:  0 -1  0 / -1  4 -1 /  0 -1  0
 *                Score = E[L^2] - (E[L])^2
 *
 *   Tenengrad  — Mean squared Sobel gradient magnitude.
 *                Gx: -1 0 1 / -2 0 2 / -1 0 1
 *                Gy: -1 -2 -1 / 0 0 0 / 1 2 1
 *                Score = mean(Gx^2 + Gy^2)
 *
 *   Brenner    — Mean of squared two-pixel horizontal differences.
 *                Score = mean((I(x+2,y) - I(x,y))^2)
 *
 * All metrics use integer math in the inner loop with 64-bit accumulators;
 * floating-point only for the final result.
 */

#include "focus.h"

#include <string.h>

static const char *metric_names[AG_FOCUS_METRIC_COUNT] = {
    "laplacian",
    "tenengrad",
    "brenner",
};

int
ag_focus_metric_from_string (const char *name)
{
    if (!name)
        return -1;
    for (int i = 0; i < AG_FOCUS_METRIC_COUNT; i++) {
        if (strcmp (name, metric_names[i]) == 0)
            return i;
    }
    return -1;
}

const char *
ag_focus_metric_name (AgFocusMetric metric)
{
    if (metric >= 0 && metric < AG_FOCUS_METRIC_COUNT)
        return metric_names[metric];
    return "unknown";
}

/* ------------------------------------------------------------------ */
/*  Laplacian (variance of 3x3 Laplacian)                             */
/* ------------------------------------------------------------------ */

static double
focus_laplacian (const uint8_t *image, int width, int height,
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

/* ------------------------------------------------------------------ */
/*  Tenengrad (Sobel gradient energy)                                  */
/* ------------------------------------------------------------------ */

static double
focus_tenengrad (const uint8_t *image, int width, int height,
                 int roi_x, int roi_y, int roi_w, int roi_h)
{
    /* Clamp ROI to valid range (1-pixel border for Sobel 3x3). */
    int x0 = roi_x < 1 ? 1 : roi_x;
    int y0 = roi_y < 1 ? 1 : roi_y;
    int x1 = roi_x + roi_w;
    int y1 = roi_y + roi_h;

    if (x1 > width  - 1) x1 = width  - 1;
    if (y1 > height - 1) y1 = height - 1;

    if (x1 - x0 < 2 || y1 - y0 < 2)
        return 0.0;

    int64_t sum_sq = 0;
    int64_t count  = 0;

    for (int y = y0; y < y1; y++) {
        const uint8_t *rp = image + (y - 1) * width;
        const uint8_t *rc = image +  y      * width;
        const uint8_t *rn = image + (y + 1) * width;

        for (int x = x0; x < x1; x++) {
            /* Sobel X: [-1 0 1; -2 0 2; -1 0 1] */
            int gx = -(int) rp[x - 1] + (int) rp[x + 1]
                   - 2 * (int) rc[x - 1] + 2 * (int) rc[x + 1]
                     - (int) rn[x - 1] + (int) rn[x + 1];

            /* Sobel Y: [-1 -2 -1; 0 0 0; 1 2 1] */
            int gy = -(int) rp[x - 1] - 2 * (int) rp[x] - (int) rp[x + 1]
                    + (int) rn[x - 1] + 2 * (int) rn[x] + (int) rn[x + 1];

            sum_sq += (int64_t) gx * gx + (int64_t) gy * gy;
            count++;
        }
    }

    if (count == 0)
        return 0.0;

    return (double) sum_sq / (double) count;
}

/* ------------------------------------------------------------------ */
/*  Brenner gradient                                                   */
/* ------------------------------------------------------------------ */

static double
focus_brenner (const uint8_t *image, int width, int height,
               int roi_x, int roi_y, int roi_w, int roi_h)
{
    /* Clamp ROI inward: need x+2 valid, so right edge stops 2 pixels
     * before image edge.  Top/bottom need no extra margin beyond
     * basic bounds checking. */
    int x0 = roi_x < 0 ? 0 : roi_x;
    int y0 = roi_y < 0 ? 0 : roi_y;
    int x1 = roi_x + roi_w;
    int y1 = roi_y + roi_h;

    if (x1 > width  - 2) x1 = width  - 2;
    if (y1 > height)     y1 = height;

    if (x1 - x0 < 1 || y1 - y0 < 1)
        return 0.0;

    int64_t sum_sq = 0;
    int64_t count  = 0;

    for (int y = y0; y < y1; y++) {
        const uint8_t *row = image + y * width;

        for (int x = x0; x < x1; x++) {
            int diff = (int) row[x + 2] - (int) row[x];
            sum_sq += (int64_t) diff * diff;
            count++;
        }
    }

    if (count == 0)
        return 0.0;

    return (double) sum_sq / (double) count;
}

/* ------------------------------------------------------------------ */
/*  Dispatch                                                           */
/* ------------------------------------------------------------------ */

double
ag_focus_score (AgFocusMetric metric,
                const uint8_t *image, int width, int height,
                int roi_x, int roi_y, int roi_w, int roi_h)
{
    switch (metric) {
    case AG_FOCUS_METRIC_LAPLACIAN:
        return focus_laplacian (image, width, height,
                                roi_x, roi_y, roi_w, roi_h);
    case AG_FOCUS_METRIC_TENENGRAD:
        return focus_tenengrad (image, width, height,
                                roi_x, roi_y, roi_w, roi_h);
    case AG_FOCUS_METRIC_BRENNER:
        return focus_brenner (image, width, height,
                              roi_x, roi_y, roi_w, roi_h);
    default:
        return focus_laplacian (image, width, height,
                                roi_x, roi_y, roi_w, roi_h);
    }
}

/* Legacy API. */
double
compute_focus_score (const uint8_t *image, int width, int height,
                     int roi_x, int roi_y, int roi_w, int roi_h)
{
    return focus_laplacian (image, width, height,
                            roi_x, roi_y, roi_w, roi_h);
}
