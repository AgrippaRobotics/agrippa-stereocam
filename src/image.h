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
int write_dual_bayer_pair (const char *output_dir,
                           const char *basename_no_ext,
                           const guint8 *interleaved,
                           guint width, guint height,
                           AgEncFormat enc,
                           int software_binning,
                           gboolean data_is_bayer,
                           const AgRemapTable *remap_left,
                           const AgRemapTable *remap_right);

#endif /* AG_IMAGE_H */
