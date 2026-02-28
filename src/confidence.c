/*
 * confidence.c — per-pixel disparity confidence / quality map
 *
 * Combines three signals into a 0–255 confidence score:
 *   1. Texture strength (Sobel gradient magnitude) — low-texture regions
 *      produce unreliable stereo matches.
 *   2. Disparity validity — invalid disparity (≤ INVALID_DISP) gets 0.
 *   3. Local disparity variance — high variance within a 3×3 window
 *      indicates noisy / unreliable matches.
 */

#include "confidence.h"

#include <math.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/*  Constants                                                          */
/* ------------------------------------------------------------------ */

#define INVALID_DISP  (-16)

/* ------------------------------------------------------------------ */
/*  Internal helpers                                                   */
/* ------------------------------------------------------------------ */

/*
 * Compute Sobel gradient magnitude for a single pixel.
 * Returns a value in approximately [0, 1020] for 8-bit input.
 */
static int
sobel_magnitude (const uint8_t *img, uint32_t width, uint32_t height,
                 uint32_t x, uint32_t y)
{
    if (x == 0 || y == 0 || x >= width - 1 || y >= height - 1)
        return 0;

    /* 3×3 Sobel kernels. */
    int p00 = img[(y - 1) * width + (x - 1)];
    int p01 = img[(y - 1) * width + x];
    int p02 = img[(y - 1) * width + (x + 1)];
    int p10 = img[y * width + (x - 1)];
    int p12 = img[y * width + (x + 1)];
    int p20 = img[(y + 1) * width + (x - 1)];
    int p21 = img[(y + 1) * width + x];
    int p22 = img[(y + 1) * width + (x + 1)];

    int gx = -p00 + p02 - 2 * p10 + 2 * p12 - p20 + p22;
    int gy = -p00 - 2 * p01 - p02 + p20 + 2 * p21 + p22;

    /* Use |gx| + |gy| as a fast approximation of sqrt(gx²+gy²). */
    if (gx < 0) gx = -gx;
    if (gy < 0) gy = -gy;
    return gx + gy;
}

/*
 * Compute local disparity variance in a 3×3 window.
 * Only considers valid pixels.  Returns variance in Q4.4² units.
 */
static double
local_variance (const int16_t *disp, uint32_t width, uint32_t height,
                uint32_t cx, uint32_t cy)
{
    int y0 = (int) cy > 0 ? (int) cy - 1 : 0;
    int y1 = cy + 1 < height ? (int) cy + 1 : (int) height - 1;
    int x0 = (int) cx > 0 ? (int) cx - 1 : 0;
    int x1 = cx + 1 < width ? (int) cx + 1 : (int) width - 1;

    double sum = 0.0;
    double sum2 = 0.0;
    int n = 0;

    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            int16_t d = disp[y * (int) width + x];
            if (d <= INVALID_DISP)
                continue;
            double v = (double) d;
            sum  += v;
            sum2 += v * v;
            n++;
        }
    }

    if (n < 2)
        return 0.0;

    double mean = sum / n;
    return (sum2 / n) - (mean * mean);
}

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

void
ag_confidence_compute (const int16_t *disparity,
                        const uint8_t *left_gray,
                        uint32_t width, uint32_t height,
                        uint8_t *confidence_out)
{
    /*
     * Score components (each normalized to [0, 1]):
     *
     * texture_score:  Sobel magnitude clamped to [0, tex_cap].
     *                 tex_cap = 200 (Sobel magnitudes above this are
     *                 all considered "good texture").
     *
     * variance_score: 1.0 when local variance is low, decaying toward 0
     *                 as variance increases.  Half-life at var_half = 400
     *                 (≈ 1.5 pixel disparity std dev in Q4.4 units).
     *
     * Final confidence = texture_score * variance_score * 255.
     * Invalid pixels get 0 unconditionally.
     */

    const double tex_cap  = 200.0;
    const double var_half = 400.0;  /* variance at which score = 0.5 */

    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            size_t idx = (size_t) y * width + x;

            if (disparity[idx] <= INVALID_DISP) {
                confidence_out[idx] = 0;
                continue;
            }

            /* Texture component. */
            int grad = sobel_magnitude (left_gray, width, height, x, y);
            double tex = (double) grad / tex_cap;
            if (tex > 1.0) tex = 1.0;

            /* Variance component. */
            double var = local_variance (disparity, width, height, x, y);
            double var_score = var_half / (var_half + var);

            /* Combined confidence. */
            double conf = tex * var_score * 255.0;
            if (conf < 0.0) conf = 0.0;
            if (conf > 255.0) conf = 255.0;

            confidence_out[idx] = (uint8_t) conf;
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Confidence visualization (JET colourmap)                           */
/* ------------------------------------------------------------------ */

/*
 * Simplified JET colour for a 0–255 input value.
 * 0 = deep blue, 128 ≈ green, 255 = deep red.
 */
static void
jet_color (uint8_t v, uint8_t *r, uint8_t *g, uint8_t *b)
{
    double t = (double) v / 255.0;

    double rr, gg, bb;

    if (t < 0.125) {
        rr = 0.0;  gg = 0.0;  bb = 0.5 + t / 0.125 * 0.5;
    } else if (t < 0.375) {
        rr = 0.0;  gg = (t - 0.125) / 0.25;  bb = 1.0;
    } else if (t < 0.625) {
        rr = (t - 0.375) / 0.25;  gg = 1.0;  bb = 1.0 - (t - 0.375) / 0.25;
    } else if (t < 0.875) {
        rr = 1.0;  gg = 1.0 - (t - 0.625) / 0.25;  bb = 0.0;
    } else {
        rr = 1.0 - (t - 0.875) / 0.125 * 0.5;  gg = 0.0;  bb = 0.0;
    }

    *r = (uint8_t) (rr * 255.0);
    *g = (uint8_t) (gg * 255.0);
    *b = (uint8_t) (bb * 255.0);
}

void
ag_confidence_colorize (const uint8_t *confidence,
                         uint32_t width, uint32_t height,
                         uint8_t *rgb_out)
{
    size_t npixels = (size_t) width * height;
    for (size_t i = 0; i < npixels; i++) {
        uint8_t *dst = rgb_out + i * 3;
        if (confidence[i] == 0) {
            dst[0] = 0;
            dst[1] = 0;
            dst[2] = 0;
        } else {
            jet_color (confidence[i], &dst[0], &dst[1], &dst[2]);
        }
    }
}
