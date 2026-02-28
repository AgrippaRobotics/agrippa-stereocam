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

/*
 * Write a single rectified image (RGB24 data already gamma-corrected,
 * debayered, and remapped).
 */
static int
write_rgb_image_raw (AgEncFormat enc, const char *path,
                     const guint8 *rgb, guint width, guint height)
{
    int ok;
    if (enc == AG_ENC_PNG)
        ok = stbi_write_png (path, (int) width, (int) height, 3, rgb,
                             (int) width * 3);
    else
        ok = stbi_write_jpg (path, (int) width, (int) height, 3, rgb, 90);

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
                       gboolean data_is_bayer,
                       const AgRemapTable *remap_left,
                       const AgRemapTable *remap_right)
{
    if (width % 2 != 0) {
        fprintf (stderr, "error: DualBayer frame width must be even, got %u\n",
                 width);
        return EXIT_FAILURE;
    }

    guint src_sub_w = width / 2;
    guint dst_w = src_sub_w;
    guint dst_h = height;

    if (software_binning > 1) {
        dst_w = src_sub_w / 2;
        dst_h = height / 2;
    }

    size_t eye_n = (size_t) dst_w * (size_t) dst_h;
    guint8 *left  = g_malloc (eye_n);
    guint8 *right = g_malloc (eye_n);
    if (!left || !right) {
        g_free (left);
        g_free (right);
        fprintf (stderr, "error: out of memory while extracting DualBayer frame\n");
        return EXIT_FAILURE;
    }

    extract_dual_bayer_eyes (interleaved, width, height, software_binning,
                             left, right);

    const char *ext = (enc == AG_ENC_PNG) ? "png"
                    : (enc == AG_ENC_JPG) ? "jpg" : "pgm";
    char *left_name  = g_strdup_printf ("%s_left.%s",  basename_no_ext, ext);
    char *right_name = g_strdup_printf ("%s_right.%s", basename_no_ext, ext);
    char *left_path  = g_build_filename (output_dir, left_name,  NULL);
    char *right_path = g_build_filename (output_dir, right_name, NULL);

    int rc_left, rc_right;

    if (remap_left && remap_right) {
        /* Rectified path: gamma -> debayer/expand -> remap -> encode. */
        apply_lut_inplace (left,  eye_n, gamma_lut_2p5 ());
        apply_lut_inplace (right, eye_n, gamma_lut_2p5 ());

        if (enc == AG_ENC_PGM) {
            /* PGM: remap grayscale, then write. */
            guint8 *rect_l = g_malloc (eye_n);
            guint8 *rect_r = g_malloc (eye_n);

            if (data_is_bayer) {
                guint8 *gray_l  = g_malloc (eye_n);
                guint8 *gray_r  = g_malloc (eye_n);

                debayer_rg8_to_gray (left,  gray_l, dst_w, dst_h);
                debayer_rg8_to_gray (right, gray_r, dst_w, dst_h);

                ag_remap_gray (remap_left,  gray_l, rect_l);
                ag_remap_gray (remap_right, gray_r, rect_r);
                g_free (gray_l);
                g_free (gray_r);
            } else {
                ag_remap_gray (remap_left,  left,  rect_l);
                ag_remap_gray (remap_right, right, rect_r);
            }

            rc_left  = write_pgm (left_path,  rect_l, dst_w, dst_h);
            rc_right = write_pgm (right_path, rect_r, dst_w, dst_h);
            g_free (rect_l);
            g_free (rect_r);
        } else {
            /* PNG/JPG: debayer/expand to RGB, remap RGB, encode. */
            guint8 *rgb_l = g_malloc (eye_n * 3);
            guint8 *rgb_r = g_malloc (eye_n * 3);
            guint8 *rect_l = g_malloc (eye_n * 3);
            guint8 *rect_r = g_malloc (eye_n * 3);

            if (data_is_bayer) {
                debayer_rg8_to_rgb (left,  rgb_l, dst_w, dst_h);
                debayer_rg8_to_rgb (right, rgb_r, dst_w, dst_h);
            } else {
                gray_to_rgb_replicate (left,  rgb_l, (uint32_t) eye_n);
                gray_to_rgb_replicate (right, rgb_r, (uint32_t) eye_n);
            }

            ag_remap_rgb (remap_left,  rgb_l, rect_l);
            ag_remap_rgb (remap_right, rgb_r, rect_r);

            rc_left  = write_rgb_image_raw (enc, left_path,  rect_l, dst_w, dst_h);
            rc_right = write_rgb_image_raw (enc, right_path, rect_r, dst_w, dst_h);

            g_free (rgb_l);
            g_free (rgb_r);
            g_free (rect_l);
            g_free (rect_r);
        }
    } else if (enc == AG_ENC_PGM) {
        rc_left  = write_pgm (left_path,  left,  dst_w, dst_h);
        rc_right = write_pgm (right_path, right, dst_w, dst_h);
    } else if (!data_is_bayer) {
        rc_left  = write_gray_image (enc, left_path,  left,  dst_w, dst_h);
        rc_right = write_gray_image (enc, right_path, right, dst_w, dst_h);
    } else {
        rc_left  = write_color_image (enc, left_path,  left,  dst_w, dst_h);
        rc_right = write_color_image (enc, right_path, right, dst_w, dst_h);
    }

    const char *kind = remap_left ? "rectified"
                     : data_is_bayer ? "BayerRG8" : "gray";
    if (rc_left == EXIT_SUCCESS && rc_right == EXIT_SUCCESS) {
        printf ("Saved: %s  (%ux%u, %s left)\n",  left_path,  dst_w, dst_h, kind);
        printf ("Saved: %s  (%ux%u, %s right)\n", right_path, dst_w, dst_h, kind);
    }

    g_free (left_name);
    g_free (right_name);
    g_free (left_path);
    g_free (right_path);
    g_free (left);
    g_free (right);

    return (rc_left == EXIT_SUCCESS && rc_right == EXIT_SUCCESS)
           ? EXIT_SUCCESS : EXIT_FAILURE;
}
