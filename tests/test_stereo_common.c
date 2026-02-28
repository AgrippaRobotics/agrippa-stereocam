/*
 * test_stereo_common.c — unit tests for stereo backend parsing,
 *                         parameter defaults, and disparity utilities
 *
 * Tests only pure-logic functions in stereo_common.c — no backend
 * creation or compute calls (those require OpenCV / ONNX Runtime).
 *
 * No camera hardware is required.
 *
 * Build:  make test
 * Run:    bin/test_stereo_common [-v]
 */

#include "../vendor/unity/unity.h"
#include "stereo.h"

#include <string.h>
#include <math.h>

void setUp (void) {}
void tearDown (void) {}

/* ------------------------------------------------------------------ */
/*  Tests: backend_parsing                                             */
/* ------------------------------------------------------------------ */

void test_parse_sgbm (void)
{
    AgStereoBackend out = AG_STEREO_ONNX;
    int rc = ag_stereo_parse_backend ("sgbm", &out);
    TEST_ASSERT_EQUAL_INT (0, rc);
    TEST_ASSERT_EQUAL_INT (AG_STEREO_SGBM, out);
}

void test_parse_onnx (void)
{
    AgStereoBackend out = AG_STEREO_SGBM;
    int rc = ag_stereo_parse_backend ("onnx", &out);
    TEST_ASSERT_EQUAL_INT (0, rc);
    TEST_ASSERT_EQUAL_INT (AG_STEREO_ONNX, out);
}

void test_parse_onnx_aliases (void)
{
    const char *aliases[] = { "igev", "rt-igev", "foundation" };
    for (size_t i = 0; i < sizeof (aliases) / sizeof (aliases[0]); i++) {
        AgStereoBackend out = AG_STEREO_SGBM;
        int rc = ag_stereo_parse_backend (aliases[i], &out);
        TEST_ASSERT_EQUAL_INT (0, rc);
        TEST_ASSERT_EQUAL_INT (AG_STEREO_ONNX, out);
    }
}

void test_parse_invalid (void)
{
    AgStereoBackend out = AG_STEREO_SGBM;
    TEST_ASSERT_EQUAL_INT (-1, ag_stereo_parse_backend ("invalid", &out));
    TEST_ASSERT_EQUAL_INT (-1, ag_stereo_parse_backend ("", &out));
}

void test_default_model_paths (void)
{
    TEST_ASSERT_EQUAL_STRING ("models/igev_plusplus.onnx",
                              ag_stereo_default_model_path ("igev"));
    TEST_ASSERT_EQUAL_STRING ("models/rt_igev_plusplus.onnx",
                              ag_stereo_default_model_path ("rt-igev"));
    TEST_ASSERT_EQUAL_STRING ("models/foundation_stereo.onnx",
                              ag_stereo_default_model_path ("foundation"));
}

void test_default_model_path_null_cases (void)
{
    TEST_ASSERT_NULL (ag_stereo_default_model_path ("sgbm"));
    TEST_ASSERT_NULL (ag_stereo_default_model_path ("onnx"));
}

void test_backend_names (void)
{
    TEST_ASSERT_EQUAL_STRING ("sgbm", ag_stereo_backend_name (AG_STEREO_SGBM));
    TEST_ASSERT_EQUAL_STRING ("onnx", ag_stereo_backend_name (AG_STEREO_ONNX));
}

/* ------------------------------------------------------------------ */
/*  Tests: sgbm_defaults                                               */
/* ------------------------------------------------------------------ */

void test_sgbm_defaults_values (void)
{
    AgSgbmParams p;
    memset (&p, 0xFF, sizeof (p));   /* poison first */
    ag_sgbm_params_defaults (&p);

    TEST_ASSERT_EQUAL_INT (0,   p.min_disparity);
    TEST_ASSERT_EQUAL_INT (128, p.num_disparities);
    TEST_ASSERT_EQUAL_INT (5,   p.block_size);
    TEST_ASSERT_EQUAL_INT (0,   p.p1);
    TEST_ASSERT_EQUAL_INT (0,   p.p2);
    TEST_ASSERT_EQUAL_INT (1,   p.disp12_max_diff);
    TEST_ASSERT_EQUAL_INT (63,  p.pre_filter_cap);
    TEST_ASSERT_EQUAL_INT (10,  p.uniqueness_ratio);
    TEST_ASSERT_EQUAL_INT (100, p.speckle_window_size);
    TEST_ASSERT_EQUAL_INT (32,  p.speckle_range);
    TEST_ASSERT_EQUAL_INT (2,   p.mode);
}

/* ------------------------------------------------------------------ */
/*  Tests: disparity_colorize — JET colourmap application              */
/* ------------------------------------------------------------------ */

void test_colorize_zero_disparity_is_black (void)
{
    enum { W = 4, H = 4 };
    int16_t disp[W * H];
    uint8_t rgb[W * H * 3];
    memset (disp, 0, sizeof (disp));

    ag_disparity_colorize (disp, W, H, 0, 128, rgb);

    /* All-zero disparity <= min*16 (0) -> black. */
    for (int i = 0; i < W * H * 3; i++)
        TEST_ASSERT_EQUAL_UINT8 (0, rgb[i]);
}

void test_colorize_below_min_is_black (void)
{
    enum { W = 2, H = 2 };
    int16_t disp[W * H];
    uint8_t rgb[W * H * 3];

    /* Set disparity below min_disparity * 16. */
    for (int i = 0; i < W * H; i++)
        disp[i] = 10;   /* min_disp=1, so threshold = 16 */

    ag_disparity_colorize (disp, W, H, 1, 128, rgb);

    for (int i = 0; i < W * H * 3; i++)
        TEST_ASSERT_EQUAL_UINT8 (0, rgb[i]);
}

void test_colorize_max_disparity_is_red (void)
{
    enum { W = 1, H = 1 };
    int16_t disp[1];
    uint8_t rgb[3];

    /* Set disparity to max: min_disp + num_disp - 1 in Q4.4. */
    int min_d = 0;
    int num_d = 128;
    disp[0] = (int16_t) ((min_d + num_d) * 16);   /* at top of range */

    ag_disparity_colorize (disp, W, H, min_d, num_d, rgb);

    /* Index clips to 255 -> deep red end of JET LUT. */
    TEST_ASSERT_TRUE (rgb[0] > 100);   /* R should be high */
    TEST_ASSERT_EQUAL_UINT8 (0, rgb[1]);   /* G should be 0 */
    TEST_ASSERT_EQUAL_UINT8 (0, rgb[2]);   /* B should be 0 */
}

void test_colorize_min_disparity_is_blue (void)
{
    enum { W = 1, H = 1 };
    int16_t disp[1];
    uint8_t rgb[3];

    /* Just above min threshold. */
    int min_d = 0;
    int num_d = 128;
    disp[0] = 1;   /* just above 0 threshold */

    ag_disparity_colorize (disp, W, H, min_d, num_d, rgb);

    /* Index ~ 0 -> deep blue end of JET LUT. */
    TEST_ASSERT_EQUAL_UINT8 (0, rgb[0]);   /* R = 0 */
    TEST_ASSERT_EQUAL_UINT8 (0, rgb[1]);   /* G = 0 */
    TEST_ASSERT_TRUE (rgb[2] > 100);   /* B should be high */
}

void test_colorize_mid_is_green_ish (void)
{
    enum { W = 1, H = 1 };
    int16_t disp[1];
    uint8_t rgb[3];

    int min_d = 0;
    int num_d = 128;
    /* Midpoint of range. */
    disp[0] = (int16_t) ((num_d / 2) * 16);

    ag_disparity_colorize (disp, W, H, min_d, num_d, rgb);

    /* Mid-range JET maps to green/cyan region. G channel should be high. */
    TEST_ASSERT_TRUE (rgb[1] > 200);
}

/* ------------------------------------------------------------------ */
/*  Tests: disparity_to_depth — inline depth conversion                */
/* ------------------------------------------------------------------ */

void test_depth_normal_case (void)
{
    /* disp_q4 = 160 -> d = 10.0 pixels
     * focal = 875.0 px, baseline = 4.07 cm
     * depth = (875.0 * 4.07) / 10.0 = 356.125 cm */
    double depth = ag_disparity_to_depth (160, 875.0, 4.07);
    TEST_ASSERT_FLOAT_WITHIN (0.01, 356.125, depth);
}

void test_depth_zero_disparity (void)
{
    double depth = ag_disparity_to_depth (0, 875.0, 4.07);
    TEST_ASSERT_EQUAL_DOUBLE (0.0, depth);
}

void test_depth_negative_disparity (void)
{
    double depth = ag_disparity_to_depth (-16, 875.0, 4.07);
    TEST_ASSERT_EQUAL_DOUBLE (0.0, depth);
}

void test_depth_one_pixel_disparity (void)
{
    /* disp_q4 = 16 -> d = 1.0 pixel
     * depth = focal * baseline / 1.0 = focal * baseline */
    double depth = ag_disparity_to_depth (16, 875.0, 4.07);
    TEST_ASSERT_FLOAT_WITHIN (0.01, 875.0 * 4.07, depth);
}

/* ------------------------------------------------------------------ */
/*  Tests: disparity_range_from_depth                                  */
/* ------------------------------------------------------------------ */

void test_range_typical_case (void)
{
    /* f=875.24, B=4.0677, z_near=30, z_far=200 */
    int min_d = 0, num_d = 0;
    int rc = ag_disparity_range_from_depth (30.0, 200.0, 875.24, 4.0677,
                                             &min_d, &num_d);
    TEST_ASSERT_EQUAL_INT (0, rc);

    /* d_min = 875.24 * 4.0677 / 200 = ~17.8 → floor → 17 */
    TEST_ASSERT_EQUAL_INT (17, min_d);

    /* d_max = 875.24 * 4.0677 / 30 = ~118.7 → ceil → 119
     * range = 119 - 17 = 102, rounded to multiple of 16 → 112 */
    TEST_ASSERT_EQUAL_INT (112, num_d);
    TEST_ASSERT_TRUE (num_d % 16 == 0);
}

void test_range_very_close (void)
{
    /* z_near=15cm should produce wider disparity range */
    int min_d = 0, num_d = 0;
    int rc = ag_disparity_range_from_depth (15.0, 200.0, 875.24, 4.0677,
                                             &min_d, &num_d);
    TEST_ASSERT_EQUAL_INT (0, rc);
    TEST_ASSERT_TRUE (num_d > 128);  /* wider than default */
    TEST_ASSERT_TRUE (num_d % 16 == 0);
}

void test_range_invalid_inputs (void)
{
    int min_d = 0, num_d = 0;

    /* z_near >= z_far */
    TEST_ASSERT_EQUAL_INT (-1, ag_disparity_range_from_depth (
        200.0, 30.0, 875.0, 4.0, &min_d, &num_d));

    /* zero depth */
    TEST_ASSERT_EQUAL_INT (-1, ag_disparity_range_from_depth (
        0.0, 200.0, 875.0, 4.0, &min_d, &num_d));

    /* negative focal length */
    TEST_ASSERT_EQUAL_INT (-1, ag_disparity_range_from_depth (
        30.0, 200.0, -1.0, 4.0, &min_d, &num_d));

    /* equal z_near and z_far */
    TEST_ASSERT_EQUAL_INT (-1, ag_disparity_range_from_depth (
        30.0, 30.0, 875.0, 4.0, &min_d, &num_d));
}

void test_range_multiple_of_16 (void)
{
    /* Ensure result is always a multiple of 16 */
    int min_d = 0, num_d = 0;
    ag_disparity_range_from_depth (25.0, 100.0, 875.24, 4.0677,
                                    &min_d, &num_d);
    TEST_ASSERT_TRUE (num_d % 16 == 0);
    TEST_ASSERT_TRUE (num_d >= 16);
}

/* ------------------------------------------------------------------ */
/*  Main                                                               */
/* ------------------------------------------------------------------ */

int
main (void)
{
    UNITY_BEGIN ();

    /* backend_parsing */
    RUN_TEST (test_parse_sgbm);
    RUN_TEST (test_parse_onnx);
    RUN_TEST (test_parse_onnx_aliases);
    RUN_TEST (test_parse_invalid);
    RUN_TEST (test_default_model_paths);
    RUN_TEST (test_default_model_path_null_cases);
    RUN_TEST (test_backend_names);

    /* sgbm_defaults */
    RUN_TEST (test_sgbm_defaults_values);

    /* disparity_colorize */
    RUN_TEST (test_colorize_zero_disparity_is_black);
    RUN_TEST (test_colorize_below_min_is_black);
    RUN_TEST (test_colorize_max_disparity_is_red);
    RUN_TEST (test_colorize_min_disparity_is_blue);
    RUN_TEST (test_colorize_mid_is_green_ish);

    /* disparity_to_depth */
    RUN_TEST (test_depth_normal_case);
    RUN_TEST (test_depth_zero_disparity);
    RUN_TEST (test_depth_negative_disparity);
    RUN_TEST (test_depth_one_pixel_disparity);

    /* disparity_range_from_depth */
    RUN_TEST (test_range_typical_case);
    RUN_TEST (test_range_very_close);
    RUN_TEST (test_range_invalid_inputs);
    RUN_TEST (test_range_multiple_of_16);

    return UNITY_END ();
}
