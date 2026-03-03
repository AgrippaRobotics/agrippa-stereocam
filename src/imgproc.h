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
void debayer_rg8_to_gray (const guint8 *bayer, guint8 *gray,
                           guint width, guint height);

/* --- RGB -> Grayscale (BT.601 luminance) --- */

void rgb_to_gray (const guint8 *rgb, guint8 *gray, uint32_t n_pixels);

/* --- Grayscale -> RGB (replicate single channel) --- */

void gray_to_rgb_replicate (const guint8 *gray, guint8 *rgb,
                             uint32_t n_pixels);

/* --- DualBayer helpers --- */

void deinterleave_dual_bayer (const guint8 *interleaved, guint width,
                               guint height, guint8 *left, guint8 *right);
void extract_dual_bayer_eyes (const guint8 *interleaved, guint width,
                               guint height, int software_binning,
                               guint8 *left, guint8 *right);
void software_bin_2x2 (const guint8 *src, guint src_w, guint src_h,
                        guint8 *dst, guint dst_w, guint dst_h);

/* --- Square crop and resize for depth export --- */

/* Round down to the nearest multiple of 32. */
static inline int round_down_32 (int x) { return x & ~31; }

/*
 * Center-crop src (w x h, `channels` per pixel) to the largest
 * inscribed square.  Writes the cropped square into dst.
 * Returns the side length of the square via *out_side.
 */
void crop_center_square (const guint8 *src, int w, int h, int channels,
                          guint8 *dst, int *out_side);

/*
 * Nearest-neighbour resize from src_side x src_side to
 * dst_side x dst_side.  Works for any channel count.
 */
void resize_nn (const guint8 *src, int src_side, int channels,
                 guint8 *dst, int dst_side);

#endif /* AG_IMGPROC_H */
