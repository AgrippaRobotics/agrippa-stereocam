/*
 * image.h — image encoding for ag-cam-tools
 */

#ifndef AG_IMAGE_H
#define AG_IMAGE_H

#include <glib.h>

#include "remap.h"

typedef enum { AG_ENC_PGM, AG_ENC_PNG, AG_ENC_JPG } AgEncFormat;

/* Parse "pgm", "png", "jpg"/"jpeg" into AgEncFormat.  Returns 0 on
 * success, -1 on unrecognised format. */
int parse_enc_format (const char *str, AgEncFormat *out);

/* Write a single-channel 8-bit PGM. */
int write_pgm (const char *path, const guint8 *data, guint width, guint height);

/* Gamma-correct + debayer + encode to PNG or JPEG. */
int write_color_image (AgEncFormat enc, const char *path,
                       const guint8 *bayer, guint width, guint height);

/* Gamma-correct + encode single-channel data as grayscale PNG/JPEG. */
int write_gray_image (AgEncFormat enc, const char *path,
                      const guint8 *gray, guint width, guint height);

/*
 * Full DualBayer pipeline: deinterleave, optional software binning,
 * gamma-correct, debayer, optional rectification, encode left+right images.
 *
 * When data_is_bayer is FALSE (binned data), PNG/JPEG output is saved
 * as grayscale instead of incorrectly debayering.
 *
 * When remap_left/remap_right are non-NULL, rectification is applied
 * after debayering (or gray expansion) and before encoding.
 * Pass NULL for both to skip rectification (backward compatible).
 */
/* Write a 4-channel RGBA PNG.  rgb is width*height*3, alpha is width*height. */
int write_rgba_png (const char *path,
                    const guint8 *rgb, const guint8 *alpha,
                    guint width, guint height);

/*
 * When HAVE_APRILTAG is defined, AprilTag overlay structs can be
 * passed for drawing onto captured images.  Forward-declare the
 * struct so the header works without the apriltag includes.
 */
#ifdef HAVE_APRILTAG
#include "apriltag_detect.h"
#endif

int write_dual_bayer_pair (const char *output_dir,
                           const char *basename_no_ext,
                           const guint8 *interleaved,
                           guint width, guint height,
                           AgEncFormat enc,
                           int software_binning,
                           gboolean data_is_bayer,
                           const AgRemapTable *remap_left,
                           const AgRemapTable *remap_right,
                           const void *left_tags, int n_left_tags,
                           const void *right_tags, int n_right_tags);

/*
 * Encode an already-split stereo pair.
 *
 * left and right must each be width*height bytes at the per-eye
 * resolution (i.e. software binning has already been applied).  The
 * function may mutate left/right in place (gamma LUT, etc.); callers
 * that need to preserve the original data must copy first.
 *
 * Semantics for data_is_bayer, remap_left/right, and the tag overlays
 * match write_dual_bayer_pair().
 */
int write_split_bayer_pair (const char *output_dir,
                            const char *basename_no_ext,
                            guint8 *left, guint8 *right,
                            guint width, guint height,
                            AgEncFormat enc,
                            gboolean data_is_bayer,
                            const AgRemapTable *remap_left,
                            const AgRemapTable *remap_right,
                            const void *left_tags, int n_left_tags,
                            const void *right_tags, int n_right_tags);

#endif /* AG_IMAGE_H */
