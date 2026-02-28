/*
 * imgproc.c — pure image-processing helpers (no hardware dependency)
 *
 * Extracted from common.c so that unit tests can link against these
 * functions without pulling in Aravis or network code.
 */

#include "imgproc.h"

#include <math.h>
#include <string.h>

/* ================================================================== */
/*  Gamma / LUT                                                       */
/* ================================================================== */

static const double k_raw_gamma = 2.5;

const guint8 *
gamma_lut_2p5 (void)
{
    static gboolean initialized = FALSE;
    static guint8 lut[256];
    if (!initialized) {
        double inv_gamma = 1.0 / k_raw_gamma;
        for (int i = 0; i < 256; i++) {
            double x = (double) i / 255.0;
            double y = pow (x, inv_gamma) * 255.0;
            if (y < 0.0) y = 0.0;
            if (y > 255.0) y = 255.0;
            lut[i] = (guint8) y;
        }
        initialized = TRUE;
    }
    return lut;
}

void
apply_lut_inplace (guint8 *data, size_t n, const guint8 lut[256])
{
    for (size_t i = 0; i < n; i++)
        data[i] = lut[data[i]];
}

/* ================================================================== */
/*  Debayer                                                           */
/* ================================================================== */

void
debayer_rg8_to_rgb (const guint8 *bayer, guint8 *rgb,
                    guint width, guint height)
{
    for (guint y = 0; y < height; y++) {
        for (guint x = 0; x < width; x++) {
#define B(dx, dy) ((int) bayer[ \
    (guint) CLAMP ((int)(y) + (dy), 0, (int)(height) - 1) * (width) + \
    (guint) CLAMP ((int)(x) + (dx), 0, (int)(width)  - 1)])

            int r, g, b;
            int ye = ((y & 1) == 0);
            int xe = ((x & 1) == 0);

            if (ye && xe) {           /* R pixel */
                r = B( 0,  0);
                g = (B(-1, 0) + B(1, 0) + B( 0,-1) + B(0, 1)) / 4;
                b = (B(-1,-1) + B(1,-1) + B(-1, 1) + B(1, 1)) / 4;
            } else if (ye && !xe) {   /* G on R row */
                r = (B(-1, 0) + B(1, 0)) / 2;
                g = B( 0,  0);
                b = (B( 0,-1) + B(0, 1)) / 2;
            } else if (!ye && xe) {   /* G on B row */
                r = (B( 0,-1) + B(0, 1)) / 2;
                g = B( 0,  0);
                b = (B(-1, 0) + B(1, 0)) / 2;
            } else {                  /* B pixel */
                r = (B(-1,-1) + B(1,-1) + B(-1, 1) + B(1, 1)) / 4;
                g = (B(-1, 0) + B(1, 0) + B( 0,-1) + B(0, 1)) / 4;
                b = B( 0,  0);
            }

#undef B
            size_t idx = ((size_t) y * width + x) * 3;
            rgb[idx + 0] = (guint8) r;
            rgb[idx + 1] = (guint8) g;
            rgb[idx + 2] = (guint8) b;
        }
    }
}

/* ================================================================== */
/*  RGB -> Grayscale (BT.601 luminance)                                */
/* ================================================================== */

void
rgb_to_gray (const guint8 *rgb, guint8 *gray, uint32_t n_pixels)
{
    for (uint32_t i = 0; i < n_pixels; i++) {
        const guint8 *p = rgb + (size_t) i * 3;
        /* BT.601:  Y = 0.299 R + 0.587 G + 0.114 B
         * Fixed-point:  Y = (77 R + 150 G + 29 B + 128) >> 8  */
        gray[i] = (guint8) ((77u * p[0] + 150u * p[1] + 29u * p[2] + 128u) >> 8);
    }
}

/* ================================================================== */
/*  Grayscale -> RGB (replicate)                                       */
/* ================================================================== */

void
gray_to_rgb_replicate (const guint8 *gray, guint8 *rgb, uint32_t n_pixels)
{
    for (uint32_t i = 0; i < n_pixels; i++) {
        rgb[(size_t) i * 3 + 0] = gray[i];
        rgb[(size_t) i * 3 + 1] = gray[i];
        rgb[(size_t) i * 3 + 2] = gray[i];
    }
}

/* ================================================================== */
/*  DualBayer helpers                                                  */
/* ================================================================== */

void
deinterleave_dual_bayer (const guint8 *interleaved, guint width,
                          guint height, guint8 *left, guint8 *right)
{
    guint sub_w = width / 2;
    for (guint y = 0; y < height; y++) {
        const guint8 *row = interleaved + ((size_t) y * (size_t) width);
        guint8 *lrow = left  + ((size_t) y * (size_t) sub_w);
        guint8 *rrow = right + ((size_t) y * (size_t) sub_w);
        for (guint x = 0; x < sub_w; x++) {
            lrow[x] = row[2 * x];
            rrow[x] = row[2 * x + 1];
        }
    }
}

void
software_bin_2x2 (const guint8 *src, guint src_w, guint src_h,
                   guint8 *dst, guint dst_w, guint dst_h)
{
    (void) src_h;
    for (guint y = 0; y < dst_h; y++) {
        guint sy = 2 * y;
        for (guint x = 0; x < dst_w; x++) {
            guint sx = 2 * x;
            size_t i00 = (size_t) sy * src_w + sx;
            size_t i01 = i00 + 1;
            size_t i10 = i00 + src_w;
            size_t i11 = i10 + 1;
            dst[(size_t) y * dst_w + x] = (guint8)
                ((src[i00] + src[i01] + src[i10] + src[i11]) / 4);
        }
    }
}
