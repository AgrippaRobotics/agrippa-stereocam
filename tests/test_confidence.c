/*
 * test_confidence.c — unit tests for disparity confidence map
 *
 * No camera hardware required.
 *
 * Build:  make test
 * Run:    bin/test_confidence [-v]
 */

#include "../vendor/unity/unity.h"
#include "confidence.h"

#include <string.h>
#include <stdlib.h>

void setUp (void) {}
void tearDown (void) {}

/* ------------------------------------------------------------------ */
/*  Helpers                                                            */
/* ------------------------------------------------------------------ */

#define INVALID_DISP  (-16)

static void
fill_disparity (int16_t *buf, int n, int16_t value)
{
    for (int i = 0; i < n; i++)
        buf[i] = value;
}

/* ------------------------------------------------------------------ */
/*  Tests: invalid disparity → zero confidence                        */
/* ------------------------------------------------------------------ */

void test_invalid_disparity_zero_confidence (void)
{
    enum { W = 8, H = 8 };
    int16_t disp[W * H];
    uint8_t gray[W * H];
    uint8_t conf[W * H];

    fill_disparity (disp, W * H, INVALID_DISP);
    memset (gray, 128, sizeof (gray));

    ag_confidence_compute (disp, gray, W, H, conf);

    for (int i = 0; i < W * H; i++)
        TEST_ASSERT_EQUAL_UINT8 (0, conf[i]);
}

/* ------------------------------------------------------------------ */
/*  Tests: uniform low-texture image → low confidence                 */
/* ------------------------------------------------------------------ */

void test_uniform_texture_low_confidence (void)
{
    enum { W = 8, H = 8 };
    int16_t disp[W * H];
    uint8_t gray[W * H];
    uint8_t conf[W * H];

    fill_disparity (disp, W * H, 100);   /* all valid */
    memset (gray, 128, sizeof (gray));    /* perfectly uniform → zero gradient */

    ag_confidence_compute (disp, gray, W, H, conf);

    /* Interior pixels with uniform texture should have very low confidence. */
    /* Edge pixels (x=0, y=0, x=W-1, y=H-1) also get low conf from border. */
    for (int i = 0; i < W * H; i++)
        TEST_ASSERT_LESS_OR_EQUAL_UINT8 (10, conf[i]);
}

/* ------------------------------------------------------------------ */
/*  Tests: strong texture → higher confidence                         */
/* ------------------------------------------------------------------ */

void test_strong_texture_higher_confidence (void)
{
    enum { W = 16, H = 16 };
    int16_t disp[W * H];
    uint8_t gray_uniform[W * H];
    uint8_t gray_textured[W * H];
    uint8_t conf_uniform[W * H];
    uint8_t conf_textured[W * H];

    fill_disparity (disp, W * H, 100);  /* all valid, uniform disparity */

    /* Uniform image: no texture. */
    memset (gray_uniform, 128, sizeof (gray_uniform));

    /* Textured image: horizontal gradient (strong Sobel response). */
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            gray_textured[y * W + x] = (uint8_t) (x * 255 / (W - 1));

    ag_confidence_compute (disp, gray_uniform,  W, H, conf_uniform);
    ag_confidence_compute (disp, gray_textured, W, H, conf_textured);

    /* Interior pixel (8,8) should have higher confidence with texture. */
    TEST_ASSERT_GREATER_THAN (conf_uniform[8 * W + 8],
                               conf_textured[8 * W + 8]);
}

/* ------------------------------------------------------------------ */
/*  Tests: high local variance → lower confidence                     */
/* ------------------------------------------------------------------ */

void test_noisy_disparity_lower_confidence (void)
{
    enum { W = 16, H = 16 };
    int16_t disp_smooth[W * H];
    int16_t disp_noisy[W * H];
    uint8_t gray[W * H];
    uint8_t conf_smooth[W * H];
    uint8_t conf_noisy[W * H];

    /* Strong texture (horizontal gradient). */
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            gray[y * W + x] = (uint8_t) (x * 255 / (W - 1));

    /* Smooth disparity: all 100. */
    fill_disparity (disp_smooth, W * H, 100);

    /* Noisy disparity: alternating 50 and 150 — high local variance. */
    for (int i = 0; i < W * H; i++)
        disp_noisy[i] = (i % 2 == 0) ? 50 : 150;

    ag_confidence_compute (disp_smooth, gray, W, H, conf_smooth);
    ag_confidence_compute (disp_noisy,  gray, W, H, conf_noisy);

    /* Noisy disparity should yield lower confidence at interior pixel (8,8). */
    TEST_ASSERT_GREATER_THAN (conf_noisy[8 * W + 8],
                               conf_smooth[8 * W + 8]);
}

/* ------------------------------------------------------------------ */
/*  Tests: colorize                                                    */
/* ------------------------------------------------------------------ */

void test_colorize_zero_is_black (void)
{
    enum { W = 4, H = 4 };
    uint8_t conf[W * H];
    uint8_t rgb[W * H * 3];

    memset (conf, 0, sizeof (conf));
    ag_confidence_colorize (conf, W, H, rgb);

    for (int i = 0; i < W * H; i++) {
        TEST_ASSERT_EQUAL_UINT8 (0, rgb[i * 3 + 0]);
        TEST_ASSERT_EQUAL_UINT8 (0, rgb[i * 3 + 1]);
        TEST_ASSERT_EQUAL_UINT8 (0, rgb[i * 3 + 2]);
    }
}

void test_colorize_max_is_red (void)
{
    enum { W = 4, H = 4 };
    uint8_t conf[W * H];
    uint8_t rgb[W * H * 3];

    memset (conf, 255, sizeof (conf));
    ag_confidence_colorize (conf, W, H, rgb);

    /* High confidence should be warm (high red channel). */
    for (int i = 0; i < W * H; i++)
        TEST_ASSERT_GREATER_THAN (100, rgb[i * 3 + 0]);
}

void test_colorize_nonzero_has_color (void)
{
    enum { W = 4, H = 4 };
    uint8_t conf[W * H];
    uint8_t rgb[W * H * 3];

    memset (conf, 128, sizeof (conf));
    ag_confidence_colorize (conf, W, H, rgb);

    /* Mid-confidence should not be pure black. */
    int any_nonzero = 0;
    for (int i = 0; i < W * H * 3; i++)
        if (rgb[i] > 0) any_nonzero = 1;
    TEST_ASSERT_TRUE (any_nonzero);
}

/* ------------------------------------------------------------------ */
/*  Tests: edge cases                                                  */
/* ------------------------------------------------------------------ */

void test_single_pixel (void)
{
    int16_t disp = 100;
    uint8_t gray = 128;
    uint8_t conf = 255;

    ag_confidence_compute (&disp, &gray, 1, 1, &conf);

    /* Single-pixel image: border pixel with uniform gradient = 0.
     * Should be low confidence due to zero texture. */
    TEST_ASSERT_LESS_OR_EQUAL_UINT8 (10, conf);
}

void test_mixed_valid_invalid (void)
{
    enum { W = 16, H = 16 };
    int16_t disp[W * H];
    uint8_t gray[W * H];
    uint8_t conf[W * H];

    /* Textured image: horizontal gradient. */
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            gray[y * W + x] = (uint8_t) (x * 255 / (W - 1));

    /* Half valid, half invalid. */
    for (int i = 0; i < W * H; i++)
        disp[i] = (i < W * H / 2) ? 100 : INVALID_DISP;

    ag_confidence_compute (disp, gray, W, H, conf);

    /* Invalid pixels must have confidence 0. */
    for (int i = W * H / 2; i < W * H; i++)
        TEST_ASSERT_EQUAL_UINT8 (0, conf[i]);

    /* Some valid pixels should have nonzero confidence. */
    int any_nonzero = 0;
    for (int i = 0; i < W * H / 2; i++)
        if (conf[i] > 0) any_nonzero = 1;
    TEST_ASSERT_TRUE (any_nonzero);
}

/* ------------------------------------------------------------------ */
/*  Main                                                               */
/* ------------------------------------------------------------------ */

int
main (void)
{
    UNITY_BEGIN ();

    /* invalid disparity */
    RUN_TEST (test_invalid_disparity_zero_confidence);

    /* texture influence */
    RUN_TEST (test_uniform_texture_low_confidence);
    RUN_TEST (test_strong_texture_higher_confidence);

    /* variance influence */
    RUN_TEST (test_noisy_disparity_lower_confidence);

    /* colorize */
    RUN_TEST (test_colorize_zero_is_black);
    RUN_TEST (test_colorize_max_is_red);
    RUN_TEST (test_colorize_nonzero_has_color);

    /* edge cases */
    RUN_TEST (test_single_pixel);
    RUN_TEST (test_mixed_valid_invalid);

    return UNITY_END ();
}
