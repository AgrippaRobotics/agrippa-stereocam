/*
 * test_image.c — unit tests for the image encoding pipeline
 *
 * Covers: parse_enc_format, write_pgm, write_gray_image,
 *         write_color_image, write_dual_bayer_pair.
 *
 * Tests write to a temporary directory that is cleaned up in teardown.
 * No camera hardware is required.
 *
 * Build:  make test
 * Run:    bin/test_image [-v]
 */

#include "../vendor/unity/unity.h"
#include "image.h"
#include "imgproc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* ------------------------------------------------------------------ */
/*  Temp directory management                                          */
/* ------------------------------------------------------------------ */

static char tmpdir[256];

void setUp (void)
{
    snprintf (tmpdir, sizeof (tmpdir), "/tmp/test_image_XXXXXX");
    char *r = mkdtemp (tmpdir);
    (void) r;
}

void tearDown (void)
{
    /* Remove all files then the directory. */
    char cmd[512];
    snprintf (cmd, sizeof (cmd), "rm -rf %s", tmpdir);
    int rc = system (cmd);
    (void) rc;
}

/* ------------------------------------------------------------------ */
/*  Helpers                                                            */
/* ------------------------------------------------------------------ */

static long
file_size (const char *path)
{
    struct stat st;
    if (stat (path, &st) != 0)
        return -1;
    return (long) st.st_size;
}

static int
file_starts_with (const char *path, const unsigned char *magic, size_t n)
{
    FILE *f = fopen (path, "rb");
    if (!f)
        return 0;
    unsigned char buf[16];
    size_t nr = fread (buf, 1, n, f);
    fclose (f);
    if (nr != n)
        return 0;
    return memcmp (buf, magic, n) == 0;
}

/*
 * Fill a buffer with a synthetic BayerRG8 pattern.
 * Same layout as debayer_rg8_to_rgb expects.
 */
static void
fill_bayer (guint8 *buf, guint w, guint h)
{
    for (guint y = 0; y < h; y++)
        for (guint x = 0; x < w; x++) {
            int ye = ((y & 1) == 0);
            int xe = ((x & 1) == 0);
            if (ye && xe)        buf[y * w + x] = 200;  /* R */
            else if (ye && !xe)  buf[y * w + x] = 150;  /* G on R row */
            else if (!ye && xe)  buf[y * w + x] = 150;  /* G on B row */
            else                 buf[y * w + x] = 100;  /* B */
        }
}

/*
 * Fill a DualBayer interleaved buffer: left pixels at even columns,
 * right pixels at odd columns.
 */
static void
fill_dual_bayer (guint8 *buf, guint total_w, guint h)
{
    for (guint y = 0; y < h; y++)
        for (guint x = 0; x < total_w; x++)
            buf[y * total_w + x] = (guint8) (((x + y) * 37) & 0xFF);
}

/* ------------------------------------------------------------------ */
/*  Tests: parse_enc_format                                            */
/* ------------------------------------------------------------------ */

void test_parse_png (void)
{
    AgEncFormat fmt;
    TEST_ASSERT_EQUAL_INT (0, parse_enc_format ("png", &fmt));
    TEST_ASSERT_EQUAL_INT (AG_ENC_PNG, fmt);
}

void test_parse_jpg (void)
{
    AgEncFormat fmt;
    TEST_ASSERT_EQUAL_INT (0, parse_enc_format ("jpg", &fmt));
    TEST_ASSERT_EQUAL_INT (AG_ENC_JPG, fmt);
}

void test_parse_jpeg_alias (void)
{
    AgEncFormat fmt;
    TEST_ASSERT_EQUAL_INT (0, parse_enc_format ("jpeg", &fmt));
    TEST_ASSERT_EQUAL_INT (AG_ENC_JPG, fmt);
}

void test_parse_pgm (void)
{
    AgEncFormat fmt;
    TEST_ASSERT_EQUAL_INT (0, parse_enc_format ("pgm", &fmt));
    TEST_ASSERT_EQUAL_INT (AG_ENC_PGM, fmt);
}

void test_parse_unknown (void)
{
    AgEncFormat fmt;
    TEST_ASSERT_EQUAL_INT (-1, parse_enc_format ("bmp", &fmt));
    TEST_ASSERT_EQUAL_INT (-1, parse_enc_format ("tiff", &fmt));
    TEST_ASSERT_EQUAL_INT (-1, parse_enc_format ("", &fmt));
}

/* ------------------------------------------------------------------ */
/*  Tests: write_pgm — P5 binary PGM output                           */
/* ------------------------------------------------------------------ */

void test_pgm_small_image (void)
{
    enum { W = 4, H = 4 };
    guint8 gray[W * H];
    for (int i = 0; i < W * H; i++)
        gray[i] = (guint8) (i * 16);

    char path[512];
    snprintf (path, sizeof (path), "%s/test.pgm", tmpdir);

    TEST_ASSERT_EQUAL_INT (EXIT_SUCCESS, write_pgm (path, gray, W, H));

    /* Verify file exists. */
    long sz = file_size (path);
    TEST_ASSERT_TRUE (sz > 0);

    /* Read back and check P5 header + dimensions. */
    FILE *f = fopen (path, "rb");
    TEST_ASSERT_NOT_NULL (f);
    char magic[4];
    int w_read, h_read, max_read;
    int rc = fscanf (f, "%2s %d %d %d ", magic, &w_read, &h_read, &max_read);
    long hdr_end = ftell (f);
    fclose (f);
    TEST_ASSERT_EQUAL_INT (4, rc);
    TEST_ASSERT_TRUE (magic[0] == 'P' && magic[1] == '5');
    TEST_ASSERT_EQUAL_INT (W, w_read);
    TEST_ASSERT_EQUAL_INT (H, h_read);
    TEST_ASSERT_EQUAL_INT (255, max_read);

    /* Total size = header + W*H. */
    TEST_ASSERT_EQUAL_INT64 ((long) (hdr_end + W * H), sz);
}

void test_pgm_pixel_data_roundtrip (void)
{
    enum { W = 8, H = 4 };
    guint8 gray[W * H];
    for (int i = 0; i < W * H; i++)
        gray[i] = (guint8) i;

    char path[512];
    snprintf (path, sizeof (path), "%s/roundtrip.pgm", tmpdir);
    TEST_ASSERT_EQUAL_INT (EXIT_SUCCESS, write_pgm (path, gray, W, H));

    /* Read back the pixel data (skip header). */
    FILE *f = fopen (path, "rb");
    TEST_ASSERT_NOT_NULL (f);

    /* Parse header to find start of data. */
    int w_read, h_read, max_read;
    char magic[4];
    int rc = fscanf (f, "%2s %d %d %d ", magic, &w_read, &h_read, &max_read);
    TEST_ASSERT_EQUAL_INT (4, rc);
    TEST_ASSERT_EQUAL_INT (W, w_read);
    TEST_ASSERT_EQUAL_INT (H, h_read);
    TEST_ASSERT_EQUAL_INT (255, max_read);

    guint8 readback[W * H];
    size_t n = fread (readback, 1, W * H, f);
    fclose (f);
    TEST_ASSERT_EQUAL_size_t ((size_t) (W * H), n);
    TEST_ASSERT_EQUAL_MEMORY (gray, readback, W * H);
}

void test_pgm_bad_path (void)
{
    guint8 gray[4] = { 0, 0, 0, 0 };
    TEST_ASSERT_EQUAL_INT (EXIT_FAILURE,
                           write_pgm ("/no/such/dir/bad.pgm", gray, 2, 2));
}

/* ------------------------------------------------------------------ */
/*  Tests: write_gray_image — grayscale PNG / JPG                      */
/* ------------------------------------------------------------------ */

void test_gray_png_magic (void)
{
    enum { W = 8, H = 8 };
    guint8 gray[W * H];
    memset (gray, 128, sizeof (gray));

    char path[512];
    snprintf (path, sizeof (path), "%s/gray.png", tmpdir);
    TEST_ASSERT_EQUAL_INT (EXIT_SUCCESS, write_gray_image (AG_ENC_PNG, path, gray, W, H));

    TEST_ASSERT_TRUE (file_size (path) > 0);
    unsigned char png_magic[] = { 0x89, 'P', 'N', 'G' };
    TEST_ASSERT_TRUE (file_starts_with (path, png_magic, 4));
}

void test_gray_jpg_magic (void)
{
    enum { W = 8, H = 8 };
    guint8 gray[W * H];
    memset (gray, 128, sizeof (gray));

    char path[512];
    snprintf (path, sizeof (path), "%s/gray.jpg", tmpdir);
    TEST_ASSERT_EQUAL_INT (EXIT_SUCCESS, write_gray_image (AG_ENC_JPG, path, gray, W, H));

    TEST_ASSERT_TRUE (file_size (path) > 0);
    unsigned char jpg_magic[] = { 0xFF, 0xD8 };
    TEST_ASSERT_TRUE (file_starts_with (path, jpg_magic, 2));
}

/* ------------------------------------------------------------------ */
/*  Tests: write_color_image — Bayer -> RGB PNG / JPG                  */
/* ------------------------------------------------------------------ */

void test_color_png_magic (void)
{
    enum { W = 8, H = 8 };
    guint8 bayer[W * H];
    fill_bayer (bayer, W, H);

    char path[512];
    snprintf (path, sizeof (path), "%s/color.png", tmpdir);
    TEST_ASSERT_EQUAL_INT (EXIT_SUCCESS, write_color_image (AG_ENC_PNG, path, bayer, W, H));

    TEST_ASSERT_TRUE (file_size (path) > 0);
    unsigned char png_magic[] = { 0x89, 'P', 'N', 'G' };
    TEST_ASSERT_TRUE (file_starts_with (path, png_magic, 4));
}

void test_color_jpg_magic (void)
{
    enum { W = 8, H = 8 };
    guint8 bayer[W * H];
    fill_bayer (bayer, W, H);

    char path[512];
    snprintf (path, sizeof (path), "%s/color.jpg", tmpdir);
    TEST_ASSERT_EQUAL_INT (EXIT_SUCCESS, write_color_image (AG_ENC_JPG, path, bayer, W, H));

    TEST_ASSERT_TRUE (file_size (path) > 0);
    unsigned char jpg_magic[] = { 0xFF, 0xD8 };
    TEST_ASSERT_TRUE (file_starts_with (path, jpg_magic, 2));
}

/* ------------------------------------------------------------------ */
/*  Tests: write_dual_bayer_pair — full DualBayer pipeline              */
/* ------------------------------------------------------------------ */

void test_dual_odd_width_rejected (void)
{
    guint8 buf[15];
    memset (buf, 0, sizeof (buf));
    TEST_ASSERT_EQUAL_INT (EXIT_FAILURE,
                           write_dual_bayer_pair (tmpdir, "bad", buf,
                                                  5, 3,  /* odd width */
                                                  AG_ENC_PNG, 0, TRUE,
                                                  NULL, NULL));
}

void test_dual_produces_left_right_png (void)
{
    /* DualBayer frame: total width must be even; sub-image width = total/2. */
    enum { TOTAL_W = 16, H = 8 };
    guint8 interleaved[TOTAL_W * H];
    fill_dual_bayer (interleaved, TOTAL_W, H);

    TEST_ASSERT_EQUAL_INT (EXIT_SUCCESS,
                           write_dual_bayer_pair (tmpdir, "pair", interleaved,
                                                  TOTAL_W, H, AG_ENC_PNG,
                                                  0, TRUE, NULL, NULL));

    char left_path[512], right_path[512];
    snprintf (left_path,  sizeof (left_path),  "%s/pair_left.png",  tmpdir);
    snprintf (right_path, sizeof (right_path), "%s/pair_right.png", tmpdir);

    TEST_ASSERT_TRUE (file_size (left_path)  > 0);
    TEST_ASSERT_TRUE (file_size (right_path) > 0);

    unsigned char png_magic[] = { 0x89, 'P', 'N', 'G' };
    TEST_ASSERT_TRUE (file_starts_with (left_path,  png_magic, 4));
    TEST_ASSERT_TRUE (file_starts_with (right_path, png_magic, 4));
}

void test_dual_produces_pgm (void)
{
    enum { TOTAL_W = 16, H = 8 };
    guint8 interleaved[TOTAL_W * H];
    fill_dual_bayer (interleaved, TOTAL_W, H);

    TEST_ASSERT_EQUAL_INT (EXIT_SUCCESS,
                           write_dual_bayer_pair (tmpdir, "pgmpair", interleaved,
                                                  TOTAL_W, H, AG_ENC_PGM,
                                                  0, TRUE, NULL, NULL));

    char left_path[512], right_path[512];
    snprintf (left_path,  sizeof (left_path),  "%s/pgmpair_left.pgm",  tmpdir);
    snprintf (right_path, sizeof (right_path), "%s/pgmpair_right.pgm", tmpdir);

    TEST_ASSERT_TRUE (file_size (left_path)  > 0);
    TEST_ASSERT_TRUE (file_size (right_path) > 0);

    /* PGM starts with "P5\n". */
    unsigned char pgm_magic[] = { 'P', '5', '\n' };
    TEST_ASSERT_TRUE (file_starts_with (left_path,  pgm_magic, 3));
    TEST_ASSERT_TRUE (file_starts_with (right_path, pgm_magic, 3));
}

void test_dual_with_binning (void)
{
    /* Binning halves each dimension.
     * Total width 32, height 16 -> sub-image 16x16 -> binned 8x8. */
    enum { TOTAL_W = 32, H = 16 };
    guint8 interleaved[TOTAL_W * H];
    fill_dual_bayer (interleaved, TOTAL_W, H);

    TEST_ASSERT_EQUAL_INT (EXIT_SUCCESS,
                           write_dual_bayer_pair (tmpdir, "binned", interleaved,
                                                  TOTAL_W, H, AG_ENC_PGM,
                                                  2, FALSE, NULL, NULL));

    char left_path[512];
    snprintf (left_path, sizeof (left_path), "%s/binned_left.pgm", tmpdir);
    TEST_ASSERT_TRUE (file_size (left_path) > 0);

    /* Read PGM header and verify dimensions = 8x8. */
    FILE *f = fopen (left_path, "rb");
    TEST_ASSERT_NOT_NULL (f);
    char magic[4];
    int w_read, h_read, max_read;
    int rc = fscanf (f, "%2s %d %d %d ", magic, &w_read, &h_read, &max_read);
    fclose (f);
    TEST_ASSERT_EQUAL_INT (4, rc);
    TEST_ASSERT_EQUAL_INT (8, w_read);
    TEST_ASSERT_EQUAL_INT (8, h_read);
}

void test_dual_gray_no_bayer_flag (void)
{
    /* When data_is_bayer is FALSE and no remap, gray_image path is used
     * (no debayering).  Output should still be valid. */
    enum { TOTAL_W = 16, H = 8 };
    guint8 interleaved[TOTAL_W * H];
    fill_dual_bayer (interleaved, TOTAL_W, H);

    TEST_ASSERT_EQUAL_INT (EXIT_SUCCESS,
                           write_dual_bayer_pair (tmpdir, "gray", interleaved,
                                                  TOTAL_W, H, AG_ENC_PNG,
                                                  0, FALSE, NULL, NULL));

    char left_path[512];
    snprintf (left_path, sizeof (left_path), "%s/gray_left.png", tmpdir);
    TEST_ASSERT_TRUE (file_size (left_path) > 0);
}

/* ------------------------------------------------------------------ */
/*  Main                                                               */
/* ------------------------------------------------------------------ */

int
main (void)
{
    UNITY_BEGIN ();

    /* enc_format */
    RUN_TEST (test_parse_png);
    RUN_TEST (test_parse_jpg);
    RUN_TEST (test_parse_jpeg_alias);
    RUN_TEST (test_parse_pgm);
    RUN_TEST (test_parse_unknown);

    /* write_pgm_suite */
    RUN_TEST (test_pgm_small_image);
    RUN_TEST (test_pgm_pixel_data_roundtrip);
    RUN_TEST (test_pgm_bad_path);

    /* write_gray_suite */
    RUN_TEST (test_gray_png_magic);
    RUN_TEST (test_gray_jpg_magic);

    /* write_color_suite */
    RUN_TEST (test_color_png_magic);
    RUN_TEST (test_color_jpg_magic);

    /* write_dual_suite */
    RUN_TEST (test_dual_odd_width_rejected);
    RUN_TEST (test_dual_produces_left_right_png);
    RUN_TEST (test_dual_produces_pgm);
    RUN_TEST (test_dual_with_binning);
    RUN_TEST (test_dual_gray_no_bayer_flag);

    return UNITY_END ();
}
