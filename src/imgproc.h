/*
 * imgproc.h — pure image-processing helpers (no hardware dependency)
 *
 * Extracted from common.c so that unit tests can link against these
 * functions without pulling in Aravis or network code.
 */

#ifndef AG_IMGPROC_H
#define AG_IMGPROC_H

#include <glib.h>
#include <stdint.h>

/* --- Gamma / LUT --- */

const guint8 *gamma_lut_2p5 (void);
void apply_lut_inplace (guint8 *data, size_t n, const guint8 lut[256]);

/* --- Debayer (BayerRG8 bilinear -> interleaved RGB) --- */

void debayer_rg8_to_rgb (const guint8 *bayer, guint8 *rgb,
                          guint width, guint height);

/* --- RGB -> Grayscale (BT.601 luminance) --- */

void rgb_to_gray (const guint8 *rgb, guint8 *gray, uint32_t n_pixels);

/* --- Grayscale -> RGB (replicate single channel) --- */

void gray_to_rgb_replicate (const guint8 *gray, guint8 *rgb,
                             uint32_t n_pixels);

/* --- DualBayer helpers --- */

void deinterleave_dual_bayer (const guint8 *interleaved, guint width,
                               guint height, guint8 *left, guint8 *right);
void software_bin_2x2 (const guint8 *src, guint src_w, guint src_h,
                        guint8 *dst, guint dst_w, guint dst_h);

#endif /* AG_IMGPROC_H */
