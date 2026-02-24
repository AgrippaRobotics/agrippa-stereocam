/*
 * image.h — image encoding for ag-cam-tools
 */

#ifndef AG_IMAGE_H
#define AG_IMAGE_H

#include <glib.h>

typedef enum { AG_ENC_PGM, AG_ENC_PNG, AG_ENC_JPG } AgEncFormat;

/* Parse "pgm", "png", "jpg"/"jpeg" into AgEncFormat.  Returns 0 on
 * success, -1 on unrecognised format. */
int parse_enc_format (const char *str, AgEncFormat *out);

/* Write a single-channel 8-bit PGM. */
int write_pgm (const char *path, const guint8 *data, guint width, guint height);

/* Gamma-correct + debayer + encode to PNG or JPEG. */
int write_color_image (AgEncFormat enc, const char *path,
                       const guint8 *bayer, guint width, guint height);

/*
 * Full DualBayer pipeline: deinterleave, optional software binning,
 * gamma-correct, debayer, encode left+right images.
 */
int write_dual_bayer_pair (const char *output_dir,
                           const char *basename_no_ext,
                           const guint8 *interleaved,
                           guint width, guint height,
                           AgEncFormat enc,
                           int software_binning);

#endif /* AG_IMAGE_H */
