/*
 * test_focus.c — unit tests for focus metrics
 *
 * Uses synthetic grayscale images to verify focus metric behaviour:
 * uniform images score near zero, sharp edges score high, blur
 * monotonicity holds, ROI clamping works, and all metrics produce
 * consistent left/right ordering.
 *
 * No camera hardware is required.
 *
 * Build:  make test
 * Run:    bin/test_focus [-v]
 */

#include "../vendor/unity/unity.h"
#include "focus.h"

#include <string.h>
#include <math.h>

void setUp (void) {}
void tearDown (void) {}

/* ------------------------------------------------------------------ */
/*  Helpers                                                            */
/* ------------------------------------------------------------------ */

/* Fill a WxH image with a horizontal step edge at the given column. */
static void
fill_edge (uint8_t *img, int w, int h, int edge_x)
{
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            img[y * w + x] = (x < edge_x) ? 0 : 255;
}

/* Fill a WxH image with a smooth horizontal gradient 0..255. */
static void
fill_gradient (uint8_t *img, int w, int h)
{
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            img[y * w + x] = (uint8_t) ((x * 255) / (w - 1));
}

/* Fill a WxH image with a uniform value. */
static void
fill_uniform (uint8_t *img, int w, int h, uint8_t val)
{
    memset (img, val, (size_t) w * h);
}

/* Simple blur: average each pixel with its 4-connected neighbours.
 * Writes result to dst (must be separate from src). */
static void
blur_3x3 (const uint8_t *src, uint8_t *dst, int w, int h)
{
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int sum = (int) src[y * w + x];
            int n = 1;
            if (x > 0)     { sum += src[y * w + x - 1]; n++; }
            if (x < w - 1) { sum += src[y * w + x + 1]; n++; }
            if (y > 0)     { sum += src[(y - 1) * w + x]; n++; }
            if (y < h - 1) { sum += src[(y + 1) * w + x]; n++; }
            dst[y * w + x] = (uint8_t) (sum / n);
        }
}

/* ================================================================== */
/*  LAPLACIAN — backward-compatible regression tests                   */
/* ================================================================== */

void test_laplacian_uniform_zero (void)
{
    enum { W = 16, H = 16 };
    uint8_t img[W * H];
    fill_uniform (img, W, H, 128);

    double score = compute_focus_score (img, W, H, 0, 0, W, H);
    TEST_ASSERT_TRUE (score < 0.01);
}

void test_laplacian_sharp_edge (void)
{
    enum { W = 16, H = 16 };
    uint8_t img[W * H];
    fill_edge (img, W, H, W / 2);

    double score = compute_focus_score (img, W, H, 0, 0, W, H);
    TEST_ASSERT_TRUE (score > 100.0);
}

void test_laplacian_gradient_moderate (void)
{
    enum { W = 16, H = 16 };
    uint8_t img[W * H];
    fill_gradient (img, W, H);

    double score = compute_focus_score (img, W, H, 0, 0, W, H);
    TEST_ASSERT_TRUE (score >= 0.0);
}

void test_laplacian_sharp_beats_gradient (void)
{
    enum { W = 16, H = 16 };
    uint8_t edge_img[W * H];
    uint8_t grad_img[W * H];

    fill_edge (edge_img, W, H, W / 2);
    fill_gradient (grad_img, W, H);

    double edge_score = compute_focus_score (edge_img, W, H, 0, 0, W, H);
    double grad_score = compute_focus_score (grad_img, W, H, 0, 0, W, H);

    TEST_ASSERT_TRUE (edge_score > grad_score);
}

void test_laplacian_known_5x5 (void)
{
    enum { W = 5, H = 5 };
    uint8_t img[W * H];
    memset (img, 0, sizeof (img));

    for (int y = 1; y <= 3; y++)
        for (int x = 1; x <= 3; x++)
            img[y * W + x] = 10;
    img[2 * W + 2] = 100;

    double score = compute_focus_score (img, W, H, 0, 0, W, H);
    double expected = 17244.444;
    TEST_ASSERT_FLOAT_WITHIN (1.0, expected, score);
}

void test_laplacian_dispatch_matches_legacy (void)
{
    enum { W = 16, H = 16 };
    uint8_t img[W * H];
    fill_edge (img, W, H, W / 2);

    double legacy = compute_focus_score (img, W, H, 0, 0, W, H);
    double dispatched = ag_focus_score (AG_FOCUS_METRIC_LAPLACIAN,
                                        img, W, H, 0, 0, W, H);
    TEST_ASSERT_EQUAL_DOUBLE (legacy, dispatched);
}

/* ================================================================== */
/*  TENENGRAD                                                          */
/* ================================================================== */

void test_tenengrad_uniform_zero (void)
{
    enum { W = 16, H = 16 };
    uint8_t img[W * H];
    fill_uniform (img, W, H, 128);

    double score = ag_focus_score (AG_FOCUS_METRIC_TENENGRAD,
                                   img, W, H, 0, 0, W, H);
    TEST_ASSERT_TRUE (score < 0.01);
}

void test_tenengrad_sharp_edge (void)
{
    enum { W = 16, H = 16 };
    uint8_t img[W * H];
    fill_edge (img, W, H, W / 2);

    double score = ag_focus_score (AG_FOCUS_METRIC_TENENGRAD,
                                   img, W, H, 0, 0, W, H);
    TEST_ASSERT_TRUE (score > 100.0);
}

void test_tenengrad_sharp_beats_gradient (void)
{
    enum { W = 16, H = 16 };
    uint8_t edge_img[W * H];
    uint8_t grad_img[W * H];

    fill_edge (edge_img, W, H, W / 2);
    fill_gradient (grad_img, W, H);

    double edge_score = ag_focus_score (AG_FOCUS_METRIC_TENENGRAD,
                                        edge_img, W, H, 0, 0, W, H);
    double grad_score = ag_focus_score (AG_FOCUS_METRIC_TENENGRAD,
                                        grad_img, W, H, 0, 0, W, H);
    TEST_ASSERT_TRUE (edge_score > grad_score);
}

/* ================================================================== */
/*  BRENNER                                                            */
/* ================================================================== */

void test_brenner_uniform_zero (void)
{
    enum { W = 16, H = 16 };
    uint8_t img[W * H];
    fill_uniform (img, W, H, 128);

    double score = ag_focus_score (AG_FOCUS_METRIC_BRENNER,
                                   img, W, H, 0, 0, W, H);
    TEST_ASSERT_TRUE (score < 0.01);
}

void test_brenner_sharp_edge (void)
{
    enum { W = 16, H = 16 };
    uint8_t img[W * H];
    fill_edge (img, W, H, W / 2);

    double score = ag_focus_score (AG_FOCUS_METRIC_BRENNER,
                                   img, W, H, 0, 0, W, H);
    TEST_ASSERT_TRUE (score > 100.0);
}

void test_brenner_sharp_beats_gradient (void)
{
    enum { W = 16, H = 16 };
    uint8_t edge_img[W * H];
    uint8_t grad_img[W * H];

    fill_edge (edge_img, W, H, W / 2);
    fill_gradient (grad_img, W, H);

    double edge_score = ag_focus_score (AG_FOCUS_METRIC_BRENNER,
                                        edge_img, W, H, 0, 0, W, H);
    double grad_score = ag_focus_score (AG_FOCUS_METRIC_BRENNER,
                                        grad_img, W, H, 0, 0, W, H);
    TEST_ASSERT_TRUE (edge_score > grad_score);
}

/* ================================================================== */
/*  BLUR MONOTONICITY — score must decrease with increasing blur       */
/* ================================================================== */

void test_laplacian_blur_monotonicity (void)
{
    enum { W = 32, H = 32 };
    uint8_t sharp[W * H], blur1[W * H], blur2[W * H];

    fill_edge (sharp, W, H, W / 2);
    blur_3x3 (sharp, blur1, W, H);
    blur_3x3 (blur1, blur2, W, H);

    double s0 = compute_focus_score (sharp, W, H, 0, 0, W, H);
    double s1 = compute_focus_score (blur1, W, H, 0, 0, W, H);
    double s2 = compute_focus_score (blur2, W, H, 0, 0, W, H);

    TEST_ASSERT_TRUE (s0 > s1);
    TEST_ASSERT_TRUE (s1 > s2);
}

void test_tenengrad_blur_monotonicity (void)
{
    enum { W = 32, H = 32 };
    uint8_t sharp[W * H], blur1[W * H], blur2[W * H];

    fill_edge (sharp, W, H, W / 2);
    blur_3x3 (sharp, blur1, W, H);
    blur_3x3 (blur1, blur2, W, H);

    double s0 = ag_focus_score (AG_FOCUS_METRIC_TENENGRAD,
                                sharp, W, H, 0, 0, W, H);
    double s1 = ag_focus_score (AG_FOCUS_METRIC_TENENGRAD,
                                blur1, W, H, 0, 0, W, H);
    double s2 = ag_focus_score (AG_FOCUS_METRIC_TENENGRAD,
                                blur2, W, H, 0, 0, W, H);

    TEST_ASSERT_TRUE (s0 > s1);
    TEST_ASSERT_TRUE (s1 > s2);
}

void test_brenner_blur_monotonicity (void)
{
    enum { W = 32, H = 32 };
    uint8_t sharp[W * H], blur1[W * H], blur2[W * H];

    fill_edge (sharp, W, H, W / 2);
    blur_3x3 (sharp, blur1, W, H);
    blur_3x3 (blur1, blur2, W, H);

    double s0 = ag_focus_score (AG_FOCUS_METRIC_BRENNER,
                                sharp, W, H, 0, 0, W, H);
    double s1 = ag_focus_score (AG_FOCUS_METRIC_BRENNER,
                                blur1, W, H, 0, 0, W, H);
    double s2 = ag_focus_score (AG_FOCUS_METRIC_BRENNER,
                                blur2, W, H, 0, 0, W, H);

    TEST_ASSERT_TRUE (s0 > s1);
    TEST_ASSERT_TRUE (s1 > s2);
}

/* ================================================================== */
/*  ROI — clamping and boundary conditions (all metrics)               */
/* ================================================================== */

void test_full_image_roi (void)
{
    enum { W = 16, H = 16 };
    uint8_t img[W * H];
    fill_edge (img, W, H, W / 2);

    double s1 = compute_focus_score (img, W, H, 0, 0, W, H);
    double s2 = compute_focus_score (img, W, H, 0, 0, W, H);
    TEST_ASSERT_EQUAL_DOUBLE (s1, s2);
}

void test_roi_clamps_to_border (void)
{
    enum { W = 16, H = 16 };
    uint8_t img[W * H];
    fill_uniform (img, W, H, 128);
    img[0] = 0;

    double score = compute_focus_score (img, W, H, 0, 0, W, H);
    TEST_ASSERT_TRUE (score < 1.0);
}

void test_degenerate_roi_returns_zero (void)
{
    enum { W = 16, H = 16 };
    uint8_t img[W * H];
    fill_uniform (img, W, H, 100);

    /* All three metrics should return 0 for degenerate ROIs. */
    for (int m = 0; m < AG_FOCUS_METRIC_COUNT; m++) {
        double s = ag_focus_score ((AgFocusMetric) m,
                                   img, W, H, 7, 7, 1, 1);
        TEST_ASSERT_EQUAL_DOUBLE (0.0, s);
    }
}

void test_roi_restricts_region (void)
{
    enum { W = 32, H = 32 };
    uint8_t img[W * H];

    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            if (y < H / 2 && x < W / 2)
                img[y * W + x] = (x < W / 4) ? 0 : 255;
            else
                img[y * W + x] = 128;
        }

    double score_uniform = compute_focus_score (img, W, H,
                                                 W / 2, H / 2,
                                                 W / 2, H / 2);
    double score_edge = compute_focus_score (img, W, H,
                                              0, 0, W / 2, H / 2);

    TEST_ASSERT_TRUE (score_edge > score_uniform);
}

/* ================================================================== */
/*  METRIC CONSISTENCY — left/right ordering is preserved              */
/* ================================================================== */

void test_all_metrics_consistent_ordering (void)
{
    /* Left image: sharp edge.  Right image: blurred edge.
     * Every metric should score left > right. */
    enum { W = 32, H = 32 };
    uint8_t left[W * H], right[W * H], tmp[W * H];

    fill_edge (left, W, H, W / 2);
    blur_3x3 (left, tmp, W, H);
    blur_3x3 (tmp, right, W, H);

    for (int m = 0; m < AG_FOCUS_METRIC_COUNT; m++) {
        double sl = ag_focus_score ((AgFocusMetric) m,
                                    left, W, H, 0, 0, W, H);
        double sr = ag_focus_score ((AgFocusMetric) m,
                                    right, W, H, 0, 0, W, H);
        TEST_ASSERT_TRUE (sl > sr);
    }
}

/* ================================================================== */
/*  NOISE SENSITIVITY — noisy uniform image should still be low        */
/* ================================================================== */

void test_noisy_uniform_low_score (void)
{
    /* Uniform 128 with deterministic +/- 1 noise.
     * Score should be non-zero but much less than a sharp edge. */
    enum { W = 32, H = 32 };
    uint8_t img[W * H];
    uint8_t edge_img[W * H];

    for (int i = 0; i < W * H; i++)
        img[i] = (uint8_t) (128 + (i % 3) - 1);  /* 127, 128, 129 repeating */
    fill_edge (edge_img, W, H, W / 2);

    for (int m = 0; m < AG_FOCUS_METRIC_COUNT; m++) {
        double noisy = ag_focus_score ((AgFocusMetric) m,
                                       img, W, H, 0, 0, W, H);
        double edge  = ag_focus_score ((AgFocusMetric) m,
                                       edge_img, W, H, 0, 0, W, H);
        TEST_ASSERT_TRUE (noisy < edge * 0.1);
    }
}

/* ================================================================== */
/*  STRING PARSER                                                      */
/* ================================================================== */

void test_metric_from_string (void)
{
    TEST_ASSERT_EQUAL_INT (AG_FOCUS_METRIC_LAPLACIAN,
                           ag_focus_metric_from_string ("laplacian"));
    TEST_ASSERT_EQUAL_INT (AG_FOCUS_METRIC_TENENGRAD,
                           ag_focus_metric_from_string ("tenengrad"));
    TEST_ASSERT_EQUAL_INT (AG_FOCUS_METRIC_BRENNER,
                           ag_focus_metric_from_string ("brenner"));
    TEST_ASSERT_EQUAL_INT (-1, ag_focus_metric_from_string ("unknown"));
    TEST_ASSERT_EQUAL_INT (-1, ag_focus_metric_from_string (NULL));
}

void test_metric_name (void)
{
    TEST_ASSERT_EQUAL_STRING ("laplacian",
                              ag_focus_metric_name (AG_FOCUS_METRIC_LAPLACIAN));
    TEST_ASSERT_EQUAL_STRING ("tenengrad",
                              ag_focus_metric_name (AG_FOCUS_METRIC_TENENGRAD));
    TEST_ASSERT_EQUAL_STRING ("brenner",
                              ag_focus_metric_name (AG_FOCUS_METRIC_BRENNER));
    TEST_ASSERT_EQUAL_STRING ("unknown",
                              ag_focus_metric_name ((AgFocusMetric) 99));
}

/* ================================================================== */
/*  PRECISION — minimum valid image                                    */
/* ================================================================== */

void test_minimum_valid_image (void)
{
    enum { W = 4, H = 4 };
    uint8_t img[W * H];
    fill_uniform (img, W, H, 50);

    double score = compute_focus_score (img, W, H, 0, 0, W, H);
    TEST_ASSERT_TRUE (score < 0.01);
}

/* ------------------------------------------------------------------ */
/*  Main                                                               */
/* ------------------------------------------------------------------ */

int
main (void)
{
    UNITY_BEGIN ();

    /* laplacian — backward-compatible regression */
    RUN_TEST (test_laplacian_uniform_zero);
    RUN_TEST (test_laplacian_sharp_edge);
    RUN_TEST (test_laplacian_gradient_moderate);
    RUN_TEST (test_laplacian_sharp_beats_gradient);
    RUN_TEST (test_laplacian_known_5x5);
    RUN_TEST (test_laplacian_dispatch_matches_legacy);

    /* tenengrad */
    RUN_TEST (test_tenengrad_uniform_zero);
    RUN_TEST (test_tenengrad_sharp_edge);
    RUN_TEST (test_tenengrad_sharp_beats_gradient);

    /* brenner */
    RUN_TEST (test_brenner_uniform_zero);
    RUN_TEST (test_brenner_sharp_edge);
    RUN_TEST (test_brenner_sharp_beats_gradient);

    /* blur monotonicity */
    RUN_TEST (test_laplacian_blur_monotonicity);
    RUN_TEST (test_tenengrad_blur_monotonicity);
    RUN_TEST (test_brenner_blur_monotonicity);

    /* ROI */
    RUN_TEST (test_full_image_roi);
    RUN_TEST (test_roi_clamps_to_border);
    RUN_TEST (test_degenerate_roi_returns_zero);
    RUN_TEST (test_roi_restricts_region);

    /* cross-metric */
    RUN_TEST (test_all_metrics_consistent_ordering);
    RUN_TEST (test_noisy_uniform_low_score);

    /* string parser */
    RUN_TEST (test_metric_from_string);
    RUN_TEST (test_metric_name);

    /* precision */
    RUN_TEST (test_minimum_valid_image);

    return UNITY_END ();
}
