/*
 * test_imgproc_extra.c — unit tests for imgproc.c functions not covered
 *                         by test_binning.c
 *
 * Covers: gamma_lut_2p5, apply_lut_inplace, rgb_to_gray (direct),
 *         gray_to_rgb_replicate, debayer_rg8_to_gray, extract_dual_bayer_eyes.
 *
 * No camera hardware is required.
 *
 * Build:  make test
 * Run:    bin/test_imgproc_extra [-v]
 */

#include "../vendor/unity/unity.h"
#include "imgproc.h"

#include <string.h>

void setUp (void) {}
void tearDown (void) {}

/* ------------------------------------------------------------------ */
/*  Tests: gamma_lut — LUT generation correctness                      */
/* ------------------------------------------------------------------ */

void test_lut_endpoints (void)
{
    const guint8 *lut = gamma_lut_2p5 ();
    TEST_ASSERT_EQUAL_UINT8 (0,   lut[0]);
    TEST_ASSERT_EQUAL_UINT8 (255, lut[255]);
}

void test_lut_monotonic (void)
{
    const guint8 *lut = gamma_lut_2p5 ();
    for (int i = 1; i < 256; i++)
        TEST_ASSERT_TRUE (lut[i] >= lut[i - 1]);
}

void test_lut_brightens_midtones (void)
{
    /* Inverse gamma (1/2.5 = 0.4) raises midtones.
     * pow(0.5, 0.4) ~ 0.758 -> LUT[128] ~ 193. */
    const guint8 *lut = gamma_lut_2p5 ();
    TEST_ASSERT_TRUE (lut[128] > 128);
}

void test_lut_idempotent (void)
{
    const guint8 *lut1 = gamma_lut_2p5 ();
    const guint8 *lut2 = gamma_lut_2p5 ();
    TEST_ASSERT_EQUAL_PTR (lut1, lut2);   /* same pointer — static init */
}

/* ------------------------------------------------------------------ */
/*  Tests: apply_lut — in-place LUT application                       */
/* ------------------------------------------------------------------ */

void test_lut_identity (void)
{
    guint8 identity[256];
    for (int i = 0; i < 256; i++)
        identity[i] = (guint8) i;

    guint8 data[] = { 0, 1, 127, 128, 254, 255 };
    guint8 orig[sizeof (data)];
    memcpy (orig, data, sizeof (data));

    apply_lut_inplace (data, sizeof (data), identity);
    TEST_ASSERT_EQUAL_MEMORY (orig, data, sizeof (data));
}

void test_lut_all_zero (void)
{
    guint8 zero_lut[256];
    memset (zero_lut, 0, sizeof (zero_lut));

    guint8 data[] = { 0, 50, 100, 200, 255 };
    apply_lut_inplace (data, sizeof (data), zero_lut);

    for (size_t i = 0; i < sizeof (data); i++)
        TEST_ASSERT_EQUAL_UINT8 (0, data[i]);
}

void test_lut_all_max (void)
{
    guint8 max_lut[256];
    memset (max_lut, 255, sizeof (max_lut));

    guint8 data[] = { 0, 50, 100, 200, 255 };
    apply_lut_inplace (data, sizeof (data), max_lut);

    for (size_t i = 0; i < sizeof (data); i++)
        TEST_ASSERT_EQUAL_UINT8 (255, data[i]);
}

void test_lut_zero_length (void)
{
    guint8 identity[256];
    for (int i = 0; i < 256; i++)
        identity[i] = (guint8) i;

    guint8 data[] = { 42 };
    apply_lut_inplace (data, 0, identity);
    TEST_ASSERT_EQUAL_UINT8 (42, data[0]);   /* untouched */
}

/* ------------------------------------------------------------------ */
/*  Tests: color_conversion — rgb_to_gray, gray_to_rgb_replicate       */
/* ------------------------------------------------------------------ */

void test_gray_pure_red (void)
{
    guint8 rgb[] = { 255, 0, 0 };
    guint8 gray;
    rgb_to_gray (rgb, &gray, 1);
    /* BT.601: (77*255 + 128) >> 8 = 77 */
    TEST_ASSERT_EQUAL_UINT8 (77, gray);
}

void test_gray_pure_green (void)
{
    guint8 rgb[] = { 0, 255, 0 };
    guint8 gray;
    rgb_to_gray (rgb, &gray, 1);
    /* (150*255 + 128) >> 8 = 149 */
    TEST_ASSERT_EQUAL_UINT8 (149, gray);
}

void test_gray_pure_blue (void)
{
    guint8 rgb[] = { 0, 0, 255 };
    guint8 gray;
    rgb_to_gray (rgb, &gray, 1);
    /* (29*255 + 128) >> 8 = 29 */
    TEST_ASSERT_EQUAL_UINT8 (29, gray);
}

void test_gray_white (void)
{
    guint8 rgb[] = { 255, 255, 255 };
    guint8 gray;
    rgb_to_gray (rgb, &gray, 1);
    /* (77+150+29)*255 + 128) >> 8 = (65280+128)>>8 = 255 */
    TEST_ASSERT_EQUAL_UINT8 (255, gray);
}

void test_gray_black (void)
{
    guint8 rgb[] = { 0, 0, 0 };
    guint8 gray;
    rgb_to_gray (rgb, &gray, 1);
    TEST_ASSERT_EQUAL_UINT8 (0, gray);
}

void test_replicate_uniform (void)
{
    guint8 gray[] = { 128 };
    guint8 rgb[3];
    gray_to_rgb_replicate (gray, rgb, 1);
    TEST_ASSERT_EQUAL_UINT8 (128, rgb[0]);
    TEST_ASSERT_EQUAL_UINT8 (128, rgb[1]);
    TEST_ASSERT_EQUAL_UINT8 (128, rgb[2]);
}

void test_replicate_zero (void)
{
    guint8 gray[] = { 0 };
    guint8 rgb[3];
    gray_to_rgb_replicate (gray, rgb, 1);
    TEST_ASSERT_EQUAL_UINT8 (0, rgb[0]);
    TEST_ASSERT_EQUAL_UINT8 (0, rgb[1]);
    TEST_ASSERT_EQUAL_UINT8 (0, rgb[2]);
}

void test_replicate_max (void)
{
    guint8 gray[] = { 255 };
    guint8 rgb[3];
    gray_to_rgb_replicate (gray, rgb, 1);
    TEST_ASSERT_EQUAL_UINT8 (255, rgb[0]);
    TEST_ASSERT_EQUAL_UINT8 (255, rgb[1]);
    TEST_ASSERT_EQUAL_UINT8 (255, rgb[2]);
}

void test_replicate_roundtrip (void)
{
    /* gray -> replicate -> rgb_to_gray should give back (approximately)
     * the original value.  BT.601 on (v,v,v) = (77+150+29)*v + 128 >> 8
     * = (256*v + 128) >> 8 = v for all v.  So exact roundtrip. */
    for (int v = 0; v < 256; v++) {
        guint8 g = (guint8) v;
        guint8 rgb[3];
        guint8 g2;
        gray_to_rgb_replicate (&g, rgb, 1);
        rgb_to_gray (rgb, &g2, 1);
        TEST_ASSERT_EQUAL_UINT8 (g, g2);
    }
}

void test_multi_pixel_conversion (void)
{
    guint8 rgb[] = { 255, 0, 0,   0, 255, 0,   0, 0, 255 };
    guint8 gray[3];
    rgb_to_gray (rgb, gray, 3);
    TEST_ASSERT_EQUAL_UINT8 (77,  gray[0]);
    TEST_ASSERT_EQUAL_UINT8 (149, gray[1]);
    TEST_ASSERT_EQUAL_UINT8 (29,  gray[2]);
}

void test_debayer_to_gray_matches_rgb_roundtrip (void)
{
    enum { W = 4, H = 4, N = W * H };
    guint8 bayer[N] = {
        10, 20, 30, 40,
        50, 60, 70, 80,
        90, 100, 110, 120,
        130, 140, 150, 160
    };
    guint8 rgb[N * 3];
    guint8 gray_from_rgb[N];
    guint8 gray_direct[N];

    debayer_rg8_to_rgb (bayer, rgb, W, H);
    rgb_to_gray (rgb, gray_from_rgb, N);
    debayer_rg8_to_gray (bayer, gray_direct, W, H);

    TEST_ASSERT_EQUAL_UINT8_ARRAY (gray_from_rgb, gray_direct, N);
}

void test_extract_dual_bayer_eyes_matches_deinterleave (void)
{
    enum { FULL_W = 8, H = 4, SUB_W = FULL_W / 2 };
    guint8 interleaved[FULL_W * H];
    guint8 left_expected[SUB_W * H];
    guint8 right_expected[SUB_W * H];
    guint8 left_actual[SUB_W * H];
    guint8 right_actual[SUB_W * H];

    for (int i = 0; i < FULL_W * H; i++)
        interleaved[i] = (guint8) i;

    deinterleave_dual_bayer (interleaved, FULL_W, H, left_expected, right_expected);
    extract_dual_bayer_eyes (interleaved, FULL_W, H, 1, left_actual, right_actual);

    TEST_ASSERT_EQUAL_UINT8_ARRAY (left_expected, left_actual, SUB_W * H);
    TEST_ASSERT_EQUAL_UINT8_ARRAY (right_expected, right_actual, SUB_W * H);
}

void test_extract_dual_bayer_eyes_matches_bin2x2_pipeline (void)
{
    enum { FULL_W = 8, H = 4, SUB_W = FULL_W / 2, BIN_W = SUB_W / 2, BIN_H = H / 2 };
    guint8 interleaved[FULL_W * H];
    guint8 left_split[SUB_W * H];
    guint8 right_split[SUB_W * H];
    guint8 left_expected[BIN_W * BIN_H];
    guint8 right_expected[BIN_W * BIN_H];
    guint8 left_actual[BIN_W * BIN_H];
    guint8 right_actual[BIN_W * BIN_H];

    for (int i = 0; i < FULL_W * H; i++)
        interleaved[i] = (guint8) (255 - i);

    deinterleave_dual_bayer (interleaved, FULL_W, H, left_split, right_split);
    software_bin_2x2 (left_split, SUB_W, H, left_expected, BIN_W, BIN_H);
    software_bin_2x2 (right_split, SUB_W, H, right_expected, BIN_W, BIN_H);
    extract_dual_bayer_eyes (interleaved, FULL_W, H, 2, left_actual, right_actual);

    TEST_ASSERT_EQUAL_UINT8_ARRAY (left_expected, left_actual, BIN_W * BIN_H);
    TEST_ASSERT_EQUAL_UINT8_ARRAY (right_expected, right_actual, BIN_W * BIN_H);
}

/* ------------------------------------------------------------------ */
/*  Tests: round_down_32                                               */
/* ------------------------------------------------------------------ */

void test_round_down_32_exact (void)
{
    TEST_ASSERT_EQUAL_INT (32,  round_down_32 (32));
    TEST_ASSERT_EQUAL_INT (64,  round_down_32 (64));
    TEST_ASSERT_EQUAL_INT (640, round_down_32 (640));
}

void test_round_down_32_non_multiple (void)
{
    TEST_ASSERT_EQUAL_INT (0,   round_down_32 (31));
    TEST_ASSERT_EQUAL_INT (32,  round_down_32 (33));
    TEST_ASSERT_EQUAL_INT (32,  round_down_32 (63));
    TEST_ASSERT_EQUAL_INT (1024, round_down_32 (1055));
}

void test_round_down_32_zero (void)
{
    TEST_ASSERT_EQUAL_INT (0, round_down_32 (0));
}

/* ------------------------------------------------------------------ */
/*  Tests: crop_center_square                                          */
/* ------------------------------------------------------------------ */

void test_crop_landscape (void)
{
    /* 6x4 -> 4x4 square, center crop removes 1 column each side. */
    enum { W = 6, H = 4, CH = 1 };
    guint8 src[W * H];
    for (int i = 0; i < W * H; i++)
        src[i] = (guint8) i;

    guint8 dst[H * H];
    int side = 0;
    crop_center_square (src, W, H, CH, dst, &side);

    TEST_ASSERT_EQUAL_INT (H, side);
    /* First row: columns 1..4 of src. */
    TEST_ASSERT_EQUAL_UINT8 (1, dst[0]);
    TEST_ASSERT_EQUAL_UINT8 (2, dst[1]);
    TEST_ASSERT_EQUAL_UINT8 (3, dst[2]);
    TEST_ASSERT_EQUAL_UINT8 (4, dst[3]);
}

void test_crop_portrait (void)
{
    /* 4x6 -> 4x4 square, center crop removes 1 row each side. */
    enum { W = 4, H = 6, CH = 1 };
    guint8 src[W * H];
    for (int i = 0; i < W * H; i++)
        src[i] = (guint8) i;

    guint8 dst[W * W];
    int side = 0;
    crop_center_square (src, W, H, CH, dst, &side);

    TEST_ASSERT_EQUAL_INT (W, side);
    /* First row of crop = row 1 of src. */
    TEST_ASSERT_EQUAL_UINT8 (4, dst[0]);
    TEST_ASSERT_EQUAL_UINT8 (5, dst[1]);
    TEST_ASSERT_EQUAL_UINT8 (6, dst[2]);
    TEST_ASSERT_EQUAL_UINT8 (7, dst[3]);
}

void test_crop_already_square (void)
{
    enum { W = 4, H = 4, CH = 1 };
    guint8 src[W * H];
    for (int i = 0; i < W * H; i++)
        src[i] = (guint8) i;

    guint8 dst[W * H];
    int side = 0;
    crop_center_square (src, W, H, CH, dst, &side);

    TEST_ASSERT_EQUAL_INT (W, side);
    TEST_ASSERT_EQUAL_MEMORY (src, dst, W * H);
}

void test_crop_rgb (void)
{
    /* 6x4 RGB (3 channels) -> 4x4 square. */
    enum { W = 6, H = 4, CH = 3 };
    guint8 src[W * H * CH];
    for (int i = 0; i < W * H * CH; i++)
        src[i] = (guint8) (i & 0xFF);

    guint8 dst[H * H * CH];
    int side = 0;
    crop_center_square (src, W, H, CH, dst, &side);
    TEST_ASSERT_EQUAL_INT (H, side);

    /* Check first pixel of crop = pixel (1,0) of src. */
    TEST_ASSERT_EQUAL_UINT8 (src[1 * CH + 0], dst[0]);
    TEST_ASSERT_EQUAL_UINT8 (src[1 * CH + 1], dst[1]);
    TEST_ASSERT_EQUAL_UINT8 (src[1 * CH + 2], dst[2]);
}

/* ------------------------------------------------------------------ */
/*  Tests: resize_nn                                                   */
/* ------------------------------------------------------------------ */

void test_resize_identity (void)
{
    enum { S = 4, CH = 1 };
    guint8 src[S * S];
    for (int i = 0; i < S * S; i++)
        src[i] = (guint8) i;

    guint8 dst[S * S];
    resize_nn (src, S, CH, dst, S);
    TEST_ASSERT_EQUAL_MEMORY (src, dst, S * S);
}

void test_resize_downscale (void)
{
    /* 4x4 -> 2x2 NN downscale: picks (0,0), (2,0), (0,2), (2,2). */
    enum { SRC = 4, DST = 2, CH = 1 };
    guint8 src[SRC * SRC];
    for (int y = 0; y < SRC; y++)
        for (int x = 0; x < SRC; x++)
            src[y * SRC + x] = (guint8) (y * 10 + x);

    guint8 dst[DST * DST];
    resize_nn (src, SRC, CH, dst, DST);

    TEST_ASSERT_EQUAL_UINT8 (0,  dst[0]);  /* (0,0) */
    TEST_ASSERT_EQUAL_UINT8 (2,  dst[1]);  /* (2,0) */
    TEST_ASSERT_EQUAL_UINT8 (20, dst[2]);  /* (0,2) */
    TEST_ASSERT_EQUAL_UINT8 (22, dst[3]);  /* (2,2) */
}

void test_resize_upscale (void)
{
    /* 2x2 -> 4x4 NN upscale: each pixel repeated in a 2x2 block. */
    enum { SRC = 2, DST = 4, CH = 1 };
    guint8 src[SRC * SRC] = { 10, 20, 30, 40 };
    guint8 dst[DST * DST];
    resize_nn (src, SRC, CH, dst, DST);

    /* Top-left 2x2 block should all be 10. */
    TEST_ASSERT_EQUAL_UINT8 (10, dst[0]);
    TEST_ASSERT_EQUAL_UINT8 (10, dst[1]);
    TEST_ASSERT_EQUAL_UINT8 (10, dst[DST]);
    TEST_ASSERT_EQUAL_UINT8 (10, dst[DST + 1]);

    /* Top-right 2x2 block should all be 20. */
    TEST_ASSERT_EQUAL_UINT8 (20, dst[2]);
    TEST_ASSERT_EQUAL_UINT8 (20, dst[3]);
}

/* ------------------------------------------------------------------ */
/*  Main                                                               */
/* ------------------------------------------------------------------ */

int
main (void)
{
    UNITY_BEGIN ();

    /* gamma_lut */
    RUN_TEST (test_lut_endpoints);
    RUN_TEST (test_lut_monotonic);
    RUN_TEST (test_lut_brightens_midtones);
    RUN_TEST (test_lut_idempotent);

    /* apply_lut */
    RUN_TEST (test_lut_identity);
    RUN_TEST (test_lut_all_zero);
    RUN_TEST (test_lut_all_max);
    RUN_TEST (test_lut_zero_length);

    /* color_conversion */
    RUN_TEST (test_gray_pure_red);
    RUN_TEST (test_gray_pure_green);
    RUN_TEST (test_gray_pure_blue);
    RUN_TEST (test_gray_white);
    RUN_TEST (test_gray_black);
    RUN_TEST (test_replicate_uniform);
    RUN_TEST (test_replicate_zero);
    RUN_TEST (test_replicate_max);
    RUN_TEST (test_replicate_roundtrip);
    RUN_TEST (test_multi_pixel_conversion);
    RUN_TEST (test_debayer_to_gray_matches_rgb_roundtrip);
    RUN_TEST (test_extract_dual_bayer_eyes_matches_deinterleave);
    RUN_TEST (test_extract_dual_bayer_eyes_matches_bin2x2_pipeline);

    /* round_down_32 */
    RUN_TEST (test_round_down_32_exact);
    RUN_TEST (test_round_down_32_non_multiple);
    RUN_TEST (test_round_down_32_zero);

    /* crop_center_square */
    RUN_TEST (test_crop_landscape);
    RUN_TEST (test_crop_portrait);
    RUN_TEST (test_crop_already_square);
    RUN_TEST (test_crop_rgb);

    /* resize_nn */
    RUN_TEST (test_resize_identity);
    RUN_TEST (test_resize_downscale);
    RUN_TEST (test_resize_upscale);

    return UNITY_END ();
}
