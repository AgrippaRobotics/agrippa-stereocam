/*
 * test_disparity_filter.c — unit tests for disparity post-processing filters
 *
 * Tests specular masking, median filter, and morphological cleanup.
 * No camera hardware required.
 *
 * Build:  make test
 * Run:    bin/test_disparity_filter [-v]
 */

#include "../vendor/unity/unity.h"
#include "disparity_filter.h"

#include <string.h>

void setUp (void) {}
void tearDown (void) {}

/* ------------------------------------------------------------------ */
/*  Helpers                                                            */
/* ------------------------------------------------------------------ */

static void
fill_disparity (int16_t *buf, int n, int16_t value)
{
    for (int i = 0; i < n; i++)
        buf[i] = value;
}

/* ------------------------------------------------------------------ */
/*  Tests: specular masking                                            */
/* ------------------------------------------------------------------ */

void test_specular_masks_saturated_pixels (void)
{
    enum { W = 8, H = 8 };
    int16_t disp[W * H];
    uint8_t left[W * H];
    uint8_t right[W * H];

    fill_disparity (disp, W * H, 100);  /* all valid */
    memset (left,  128, sizeof (left));
    memset (right, 128, sizeof (right));

    /* Place a specular highlight in the left image at (3,3). */
    left[3 * W + 3] = 255;

    ag_disparity_mask_specular (disp, left, right, W, H, 250, 0);

    /* The highlighted pixel should be invalidated. */
    TEST_ASSERT_TRUE (disp[3 * W + 3] <= 0);

    /* Non-highlighted pixels should remain valid. */
    TEST_ASSERT_EQUAL_INT16 (100, disp[0 * W + 0]);
    TEST_ASSERT_EQUAL_INT16 (100, disp[7 * W + 7]);
}

void test_specular_dilation_expands_mask (void)
{
    enum { W = 8, H = 8 };
    int16_t disp[W * H];
    uint8_t left[W * H];
    uint8_t right[W * H];

    fill_disparity (disp, W * H, 100);
    memset (left,  128, sizeof (left));
    memset (right, 128, sizeof (right));

    /* Highlight at (4,4). */
    left[4 * W + 4] = 252;

    ag_disparity_mask_specular (disp, left, right, W, H, 250, 1);

    /* The highlighted pixel and its immediate neighbors should be invalid. */
    TEST_ASSERT_TRUE (disp[4 * W + 4] <= 0);
    TEST_ASSERT_TRUE (disp[3 * W + 4] <= 0);  /* above */
    TEST_ASSERT_TRUE (disp[5 * W + 4] <= 0);  /* below */
    TEST_ASSERT_TRUE (disp[4 * W + 3] <= 0);  /* left */
    TEST_ASSERT_TRUE (disp[4 * W + 5] <= 0);  /* right */

    /* Pixel far from highlight should remain valid. */
    TEST_ASSERT_EQUAL_INT16 (100, disp[0 * W + 0]);
}

void test_specular_no_highlights_no_change (void)
{
    enum { W = 4, H = 4 };
    int16_t disp[W * H];
    uint8_t left[W * H];
    uint8_t right[W * H];

    fill_disparity (disp, W * H, 80);
    memset (left,  100, sizeof (left));
    memset (right, 100, sizeof (right));

    ag_disparity_mask_specular (disp, left, right, W, H, 250, 2);

    /* No pixels above threshold → no changes. */
    for (int i = 0; i < W * H; i++)
        TEST_ASSERT_EQUAL_INT16 (80, disp[i]);
}

void test_specular_checks_right_image_too (void)
{
    enum { W = 4, H = 4 };
    int16_t disp[W * H];
    uint8_t left[W * H];
    uint8_t right[W * H];

    fill_disparity (disp, W * H, 100);
    memset (left,  128, sizeof (left));
    memset (right, 128, sizeof (right));

    /* Highlight only in right image. */
    right[2 * W + 2] = 254;

    ag_disparity_mask_specular (disp, left, right, W, H, 250, 0);

    TEST_ASSERT_TRUE (disp[2 * W + 2] <= 0);
}

/* ------------------------------------------------------------------ */
/*  Tests: median filter                                               */
/* ------------------------------------------------------------------ */

void test_median_removes_outlier (void)
{
    enum { W = 5, H = 5 };
    int16_t input[W * H];
    int16_t output[W * H];

    /* Flat surface at disparity 80, one outlier at center. */
    fill_disparity (input, W * H, 80);
    input[2 * W + 2] = 200;  /* outlier */

    ag_disparity_median_filter (input, output, W, H, 3);

    /* The outlier should be corrected to 80 (the median of its neighbors). */
    TEST_ASSERT_EQUAL_INT16 (80, output[2 * W + 2]);

    /* Non-outlier pixels should remain 80. */
    TEST_ASSERT_EQUAL_INT16 (80, output[1 * W + 1]);
}

void test_median_preserves_invalid (void)
{
    enum { W = 5, H = 5 };
    int16_t input[W * H];
    int16_t output[W * H];

    fill_disparity (input, W * H, 80);
    input[2 * W + 2] = -16;  /* invalid */

    ag_disparity_median_filter (input, output, W, H, 3);

    /* Invalid pixel should remain invalid. */
    TEST_ASSERT_TRUE (output[2 * W + 2] <= 0);
}

void test_median_kernel5 (void)
{
    enum { W = 7, H = 7 };
    int16_t input[W * H];
    int16_t output[W * H];

    fill_disparity (input, W * H, 50);
    input[3 * W + 3] = 200;  /* outlier */

    ag_disparity_median_filter (input, output, W, H, 5);

    /* With kernel=5, the outlier is a small minority → median is 50. */
    TEST_ASSERT_EQUAL_INT16 (50, output[3 * W + 3]);
}

/* ------------------------------------------------------------------ */
/*  Tests: morphological cleanup                                       */
/* ------------------------------------------------------------------ */

void test_morph_close_fills_small_hole (void)
{
    enum { W = 7, H = 7 };
    int16_t disp[W * H];

    fill_disparity (disp, W * H, 60);
    /* Create a single invalid pixel surrounded by valid. */
    disp[3 * W + 3] = -16;

    ag_disparity_morph_cleanup (disp, W, H, 1, 0);

    /* Close should fill the hole — pixel should now be valid. */
    TEST_ASSERT_TRUE (disp[3 * W + 3] > 0);
    /* The filled value should be close to the surrounding 60. */
    TEST_ASSERT_INT_WITHIN (5, 60, disp[3 * W + 3]);
}

void test_morph_open_removes_small_bump (void)
{
    enum { W = 7, H = 7 };
    int16_t disp[W * H];

    /* All invalid except one isolated pixel. */
    fill_disparity (disp, W * H, -16);
    disp[3 * W + 3] = 100;

    ag_disparity_morph_cleanup (disp, W, H, 0, 1);

    /* Open should remove the isolated pixel. */
    TEST_ASSERT_TRUE (disp[3 * W + 3] <= 0);
}

void test_morph_noop_when_radii_zero (void)
{
    enum { W = 5, H = 5 };
    int16_t disp[W * H];
    int16_t copy[W * H];

    fill_disparity (disp, W * H, 50);
    disp[2 * W + 2] = -16;
    memcpy (copy, disp, sizeof (disp));

    ag_disparity_morph_cleanup (disp, W, H, 0, 0);

    /* No change when both radii are 0. */
    TEST_ASSERT_EQUAL_INT16_ARRAY (copy, disp, W * H);
}

/* ------------------------------------------------------------------ */
/*  Main                                                               */
/* ------------------------------------------------------------------ */

int
main (void)
{
    UNITY_BEGIN ();

    /* specular masking */
    RUN_TEST (test_specular_masks_saturated_pixels);
    RUN_TEST (test_specular_dilation_expands_mask);
    RUN_TEST (test_specular_no_highlights_no_change);
    RUN_TEST (test_specular_checks_right_image_too);

    /* median filter */
    RUN_TEST (test_median_removes_outlier);
    RUN_TEST (test_median_preserves_invalid);
    RUN_TEST (test_median_kernel5);

    /* morphological cleanup */
    RUN_TEST (test_morph_close_fills_small_hole);
    RUN_TEST (test_morph_open_removes_small_bump);
    RUN_TEST (test_morph_noop_when_radii_zero);

    return UNITY_END ();
}
