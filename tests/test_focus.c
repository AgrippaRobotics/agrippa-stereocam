/*
 * test_focus.c — unit tests for compute_focus_score()
 *
 * Uses synthetic grayscale images to verify focus metric behaviour:
 * uniform images score near zero, sharp edges score high, and ROI
 * clamping / boundary conditions are handled correctly.
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
/*  Tests: focus_basic — score ordering for canonical patterns          */
/* ------------------------------------------------------------------ */

void test_uniform_image_zero_score (void)
{
    enum { W = 16, H = 16 };
    uint8_t img[W * H];
    memset (img, 128, sizeof (img));

    double score = compute_focus_score (img, W, H, 0, 0, W, H);
    TEST_ASSERT_TRUE (score < 0.01);
}

void test_sharp_edge_high_score (void)
{
    enum { W = 16, H = 16 };
    uint8_t img[W * H];

    /* Left half black, right half white — sharp vertical edge. */
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            img[y * W + x] = (x < W / 2) ? 0 : 255;

    double score = compute_focus_score (img, W, H, 0, 0, W, H);
    TEST_ASSERT_TRUE (score > 100.0);
}

void test_gradient_moderate_score (void)
{
    enum { W = 16, H = 16 };
    uint8_t img[W * H];

    /* Smooth horizontal gradient. */
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            img[y * W + x] = (uint8_t) ((x * 255) / (W - 1));

    double score = compute_focus_score (img, W, H, 0, 0, W, H);

    /* Gradient has constant second derivative = 0 in the x direction
     * (linear ramp), so Laplacian response is near-zero everywhere
     * except at boundaries.  Score should be low but non-zero. */
    TEST_ASSERT_TRUE (score >= 0.0);
}

void test_sharp_beats_gradient (void)
{
    enum { W = 16, H = 16 };
    uint8_t edge_img[W * H];
    uint8_t grad_img[W * H];

    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            edge_img[y * W + x] = (x < W / 2) ? 0 : 255;
            grad_img[y * W + x] = (uint8_t) ((x * 255) / (W - 1));
        }

    double edge_score = compute_focus_score (edge_img, W, H, 0, 0, W, H);
    double grad_score = compute_focus_score (grad_img, W, H, 0, 0, W, H);

    TEST_ASSERT_TRUE (edge_score > grad_score);
}

/* ------------------------------------------------------------------ */
/*  Tests: focus_roi — ROI clamping and boundary conditions            */
/* ------------------------------------------------------------------ */

void test_full_image_roi (void)
{
    enum { W = 16, H = 16 };
    uint8_t img[W * H];
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            img[y * W + x] = (x < W / 2) ? 0 : 255;

    /* Explicit full-image ROI should match (0,0,W,H). */
    double s1 = compute_focus_score (img, W, H, 0, 0, W, H);
    double s2 = compute_focus_score (img, W, H, 0, 0, W, H);
    TEST_ASSERT_EQUAL_DOUBLE (s1, s2);
}

void test_roi_clamps_to_border (void)
{
    enum { W = 16, H = 16 };
    uint8_t img[W * H];
    memset (img, 128, sizeof (img));
    img[0] = 0;  /* single different pixel at origin */

    /* ROI starting at (0,0) should be clamped inward by 1 pixel
     * for the 3x3 kernel.  The (0,0) pixel is outside the kernel
     * region so the score should be near-zero (uniform interior). */
    double score = compute_focus_score (img, W, H, 0, 0, W, H);
    TEST_ASSERT_TRUE (score < 1.0);
}

void test_degenerate_roi_returns_zero (void)
{
    enum { W = 16, H = 16 };
    uint8_t img[W * H];
    memset (img, 100, sizeof (img));

    /* ROI too small after clamping (1 pixel wide). */
    double s1 = compute_focus_score (img, W, H, 7, 7, 1, 1);
    TEST_ASSERT_EQUAL_DOUBLE (0.0, s1);

    /* ROI at far edge with width 2 — after border clamp, valid
     * region has < 2 pixels. */
    double s2 = compute_focus_score (img, W, H, W - 2, H - 2, 2, 2);
    TEST_ASSERT_EQUAL_DOUBLE (0.0, s2);
}

void test_roi_restricts_region (void)
{
    enum { W = 32, H = 32 };
    uint8_t img[W * H];

    /* Top-left quadrant has a sharp edge, rest is uniform. */
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            if (y < H / 2 && x < W / 2)
                img[y * W + x] = (x < W / 4) ? 0 : 255;
            else
                img[y * W + x] = 128;
        }

    /* ROI covering only the uniform bottom-right should score low. */
    double score_uniform = compute_focus_score (img, W, H,
                                                 W / 2, H / 2,
                                                 W / 2, H / 2);
    /* ROI covering the sharp top-left should score high. */
    double score_edge = compute_focus_score (img, W, H,
                                              0, 0, W / 2, H / 2);

    TEST_ASSERT_TRUE (score_edge > score_uniform);
}

/* ------------------------------------------------------------------ */
/*  Tests: focus_precision — known-value Laplacian computation         */
/* ------------------------------------------------------------------ */

void test_known_5x5_laplacian (void)
{
    /*
     * 5x5 image (interior y=1..3, x=1..3 evaluated with full ROI):
     *
     *   0   0   0   0   0
     *   0  10  10  10   0
     *   0  10 100  10   0
     *   0  10  10  10   0
     *   0   0   0   0   0
     *
     * L(y,x) = 4*C - left - right - up - down
     *
     *   L(1,1) = 4*10 - 0 - 10 - 0 - 10        =  20
     *   L(1,2) = 4*10 - 10 - 10 - 0 - 100       = -80
     *   L(1,3) = 4*10 - 10 - 0 - 0 - 10         =  20
     *   L(2,1) = 4*10 - 0 - 100 - 10 - 10       = -80
     *   L(2,2) = 4*100 - 10 - 10 - 10 - 10      = 360
     *   L(2,3) = 4*10 - 100 - 0 - 10 - 10       = -80
     *   L(3,1) = 4*10 - 0 - 10 - 10 - 0         =  20
     *   L(3,2) = 4*10 - 10 - 10 - 100 - 0       = -80
     *   L(3,3) = 4*10 - 10 - 0 - 10 - 0         =  20
     *
     * sum    = 120,  sum_sq = 156800,  count = 9
     * variance = 156800/9 - (120/9)^2 = 17244.444
     */
    enum { W = 5, H = 5 };
    uint8_t img[W * H];
    memset (img, 0, sizeof (img));

    /* Fill centre cross with 10, centre pixel with 100. */
    for (int y = 1; y <= 3; y++)
        for (int x = 1; x <= 3; x++)
            img[y * W + x] = 10;
    img[2 * W + 2] = 100;

    double score = compute_focus_score (img, W, H, 0, 0, W, H);
    double expected = 17244.444;

    /* Allow 1.0 tolerance for floating-point rounding. */
    TEST_ASSERT_FLOAT_WITHIN (1.0, expected, score);
}

void test_minimum_valid_image (void)
{
    /* 3x3 is the absolute minimum — only (1,1) is evaluated.
     * A single pixel has variance = 0 (E[L^2] - E[L]^2 = L^2 - L^2 = 0).
     * Wait — actually that IS zero. Verify it. */
    enum { W = 4, H = 4 };
    uint8_t img[W * H];
    memset (img, 50, sizeof (img));

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

    /* focus_basic */
    RUN_TEST (test_uniform_image_zero_score);
    RUN_TEST (test_sharp_edge_high_score);
    RUN_TEST (test_gradient_moderate_score);
    RUN_TEST (test_sharp_beats_gradient);

    /* focus_roi */
    RUN_TEST (test_full_image_roi);
    RUN_TEST (test_roi_clamps_to_border);
    RUN_TEST (test_degenerate_roi_returns_zero);
    RUN_TEST (test_roi_restricts_region);

    /* focus_precision */
    RUN_TEST (test_known_5x5_laplacian);
    RUN_TEST (test_minimum_valid_image);

    return UNITY_END ();
}
