/*
 * image.c — image encoding for ag-cam-tools
 *
 * This is the single compilation unit that defines the stb_image_write
 * implementation.
 */

#include "image.h"
#include "common.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../vendor/stb_image_write.h"
#pragma GCC diagnostic pop

int
parse_enc_format (const char *str, AgEncFormat *out)
{
    if (strcmp (str, "png") == 0)
        { *out = AG_ENC_PNG; return 0; }
    if (strcmp (str, "jpg") == 0 || strcmp (str, "jpeg") == 0)
        { *out = AG_ENC_JPG; return 0; }
    if (strcmp (str, "pgm") == 0)
        { *out = AG_ENC_PGM; return 0; }
    return -1;
}

int
write_pgm (const char *path, const guint8 *data, guint width, guint height)
{
    FILE *f = fopen (path, "wb");
    if (!f) {
        fprintf (stderr, "error: cannot open '%s' for write: %s\n",
                 path, strerror (errno));
        return EXIT_FAILURE;
    }

    fprintf (f, "P5\n%u %u\n255\n", width, height);
    size_t n = (size_t) width * (size_t) height;
    size_t written = fwrite (data, 1, n, f);
    fclose (f);

    if (written != n) {
        fprintf (stderr, "error: short write to '%s'\n", path);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

int
write_color_image (AgEncFormat enc, const char *path,
                   const guint8 *bayer, guint width, guint height)
{
    size_t bayer_n = (size_t) width * (size_t) height;
    guint8 *gamma_bayer = g_malloc (bayer_n);
    guint8 *rgb = g_malloc (bayer_n * 3);
    if (!gamma_bayer || !rgb) {
        g_free (gamma_bayer);
        g_free (rgb);
        fprintf (stderr, "error: out of memory for debayer buffer\n");
        return EXIT_FAILURE;
    }

    memcpy (gamma_bayer, bayer, bayer_n);
    apply_lut_inplace (gamma_bayer, bayer_n, gamma_lut_2p5 ());
    debayer_rg8_to_rgb (gamma_bayer, rgb, width, height);

    int ok;
    if (enc == AG_ENC_PNG)
        ok = stbi_write_png (path, (int) width, (int) height, 3, rgb,
                             (int) width * 3);
    else
        ok = stbi_write_jpg (path, (int) width, (int) height, 3, rgb, 90);

    g_free (gamma_bayer);
    g_free (rgb);

    if (!ok) {
        fprintf (stderr, "error: failed to write '%s'\n", path);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

int
write_gray_image (AgEncFormat enc, const char *path,
                  const guint8 *gray, guint width, guint height)
{
    size_t n = (size_t) width * (size_t) height;
    guint8 *gamma_gray = g_malloc (n);
    if (!gamma_gray) {
        fprintf (stderr, "error: out of memory for gamma buffer\n");
        return EXIT_FAILURE;
    }

    memcpy (gamma_gray, gray, n);
    apply_lut_inplace (gamma_gray, n, gamma_lut_2p5 ());

    int ok;
    if (enc == AG_ENC_PNG)
        ok = stbi_write_png (path, (int) width, (int) height, 1, gamma_gray,
                             (int) width);
    else
        ok = stbi_write_jpg (path, (int) width, (int) height, 1, gamma_gray, 90);

    g_free (gamma_gray);

    if (!ok) {
        fprintf (stderr, "error: failed to write '%s'\n", path);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

int
write_dual_bayer_pair (const char *output_dir,
                       const char *basename_no_ext,
                       const guint8 *interleaved,
                       guint width, guint height,
                       AgEncFormat enc,
                       int software_binning,
                       gboolean data_is_bayer)
{
    if (width % 2 != 0) {
        fprintf (stderr, "error: DualBayer frame width must be even, got %u\n",
                 width);
        return EXIT_FAILURE;
    }

    guint src_sub_w = width / 2;
    size_t src_sub_n = (size_t) src_sub_w * (size_t) height;
    guint8 *left_src  = g_malloc (src_sub_n);
    guint8 *right_src = g_malloc (src_sub_n);
    if (!left_src || !right_src) {
        g_free (left_src);
        g_free (right_src);
        fprintf (stderr, "error: out of memory while splitting DualBayer frame\n");
        return EXIT_FAILURE;
    }

    deinterleave_dual_bayer (interleaved, width, height, left_src, right_src);

    guint dst_w = src_sub_w;
    guint dst_h = height;
    guint8 *left  = left_src;
    guint8 *right = right_src;
    guint8 *left_bin  = NULL;
    guint8 *right_bin = NULL;

    if (software_binning > 1) {
        dst_w = src_sub_w / 2;
        dst_h = height / 2;
        size_t dst_n = (size_t) dst_w * (size_t) dst_h;
        left_bin  = g_malloc (dst_n);
        right_bin = g_malloc (dst_n);
        if (!left_bin || !right_bin) {
            g_free (left_bin);
            g_free (right_bin);
            g_free (left_src);
            g_free (right_src);
            fprintf (stderr, "error: out of memory while software-binning\n");
            return EXIT_FAILURE;
        }
        software_bin_2x2 (left_src,  src_sub_w, height, left_bin,  dst_w, dst_h);
        software_bin_2x2 (right_src, src_sub_w, height, right_bin, dst_w, dst_h);
        left  = left_bin;
        right = right_bin;
    }

    const char *ext = (enc == AG_ENC_PNG) ? "png"
                    : (enc == AG_ENC_JPG) ? "jpg" : "pgm";
    char *left_name  = g_strdup_printf ("%s_left.%s",  basename_no_ext, ext);
    char *right_name = g_strdup_printf ("%s_right.%s", basename_no_ext, ext);
    char *left_path  = g_build_filename (output_dir, left_name,  NULL);
    char *right_path = g_build_filename (output_dir, right_name, NULL);

    int rc_left, rc_right;
    if (enc == AG_ENC_PGM) {
        rc_left  = write_pgm (left_path,  left,  dst_w, dst_h);
        rc_right = write_pgm (right_path, right, dst_w, dst_h);
    } else if (!data_is_bayer) {
        rc_left  = write_gray_image (enc, left_path,  left,  dst_w, dst_h);
        rc_right = write_gray_image (enc, right_path, right, dst_w, dst_h);
    } else {
        rc_left  = write_color_image (enc, left_path,  left,  dst_w, dst_h);
        rc_right = write_color_image (enc, right_path, right, dst_w, dst_h);
    }

    const char *kind = data_is_bayer ? "BayerRG8" : "gray";
    if (rc_left == EXIT_SUCCESS && rc_right == EXIT_SUCCESS) {
        printf ("Saved: %s  (%ux%u, %s left)\n",  left_path,  dst_w, dst_h, kind);
        printf ("Saved: %s  (%ux%u, %s right)\n", right_path, dst_w, dst_h, kind);
    }

    g_free (left_name);
    g_free (right_name);
    g_free (left_path);
    g_free (right_path);
    g_free (left_bin);
    g_free (right_bin);
    g_free (left_src);
    g_free (right_src);

    return (rc_left == EXIT_SUCCESS && rc_right == EXIT_SUCCESS)
           ? EXIT_SUCCESS : EXIT_FAILURE;
}
