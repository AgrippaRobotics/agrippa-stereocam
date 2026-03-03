/*
 * test_temporal_filter.c — unit tests for temporal disparity median filter
 *
 * No camera hardware required.
 *
 * Build:  make test
 * Run:    bin/test_temporal_filter [-v]
 */

#include "../vendor/unity/unity.h"
#include "temporal_filter.h"

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
/*  Tests: creation and destruction                                    */
/* ------------------------------------------------------------------ */

void test_create_destroy (void)
{
    AgTemporalFilter *f = ag_temporal_filter_create (10, 10, 3);
    TEST_ASSERT_NOT_NULL (f);
    ag_temporal_filter_destroy (f);
}

void test_create_invalid_args (void)
{
    /* depth < 2 */
    TEST_ASSERT_NULL (ag_temporal_filter_create (10, 10, 1));
    TEST_ASSERT_NULL (ag_temporal_filter_create (10, 10, 0));
    /* zero dimensions */
    TEST_ASSERT_NULL (ag_temporal_filter_create (0, 10, 3));
    TEST_ASSERT_NULL (ag_temporal_filter_create (10, 0, 3));
}

void test_destroy_null_safe (void)
{
    ag_temporal_filter_destroy (NULL);  /* should not crash */
}

/* ------------------------------------------------------------------ */
/*  Tests: single frame passthrough                                    */
/* ------------------------------------------------------------------ */

void test_single_frame_passthrough (void)
{
    enum { W = 4, H = 4 };
    AgTemporalFilter *f = ag_temporal_filter_create (W, H, 3);
    TEST_ASSERT_NOT_NULL (f);

    int16_t input[W * H];
    int16_t output[W * H];
    fill_disparity (input, W * H, 80);

    int rc = ag_temporal_filter_push (f, input, output);
    TEST_ASSERT_EQUAL_INT (0, rc);

    /* With only 1 frame, output should equal input. */
    TEST_ASSERT_EQUAL_INT16_ARRAY (input, output, W * H);

    ag_temporal_filter_destroy (f);
}

/* ------------------------------------------------------------------ */
/*  Tests: median computation                                          */
/* ------------------------------------------------------------------ */

void test_median_odd_depth (void)
{
    enum { W = 4, H = 4, DEPTH = 3 };
    AgTemporalFilter *f = ag_temporal_filter_create (W, H, DEPTH);
    TEST_ASSERT_NOT_NULL (f);

    int16_t frame[W * H];
    int16_t output[W * H];

    /* Push 3 frames with values 50, 100, 70 at every pixel.
     * Sorted: 50, 70, 100.  Median = 70. */
    fill_disparity (frame, W * H, 50);
    ag_temporal_filter_push (f, frame, output);

    fill_disparity (frame, W * H, 100);
    ag_temporal_filter_push (f, frame, output);

    fill_disparity (frame, W * H, 70);
    ag_temporal_filter_push (f, frame, output);

    for (int i = 0; i < W * H; i++)
        TEST_ASSERT_EQUAL_INT16 (70, output[i]);

    ag_temporal_filter_destroy (f);
}

void test_median_even_depth (void)
{
    enum { W = 4, H = 4, DEPTH = 4 };
    AgTemporalFilter *f = ag_temporal_filter_create (W, H, DEPTH);
    TEST_ASSERT_NOT_NULL (f);

    int16_t frame[W * H];
    int16_t output[W * H];

    /* Push 4 frames: 40, 60, 80, 100.
     * Sorted: 40, 60, 80, 100.  Even count → average of 60 and 80 = 70. */
    fill_disparity (frame, W * H, 40);
    ag_temporal_filter_push (f, frame, output);

    fill_disparity (frame, W * H, 60);
    ag_temporal_filter_push (f, frame, output);

    fill_disparity (frame, W * H, 80);
    ag_temporal_filter_push (f, frame, output);

    fill_disparity (frame, W * H, 100);
    ag_temporal_filter_push (f, frame, output);

    for (int i = 0; i < W * H; i++)
        TEST_ASSERT_EQUAL_INT16 (70, output[i]);

    ag_temporal_filter_destroy (f);
}

void test_median_suppresses_outlier (void)
{
    enum { W = 4, H = 4, DEPTH = 5 };
    AgTemporalFilter *f = ag_temporal_filter_create (W, H, DEPTH);
    TEST_ASSERT_NOT_NULL (f);

    int16_t frame[W * H];
    int16_t output[W * H];

    /* Push 5 frames: 80, 80, 200, 80, 80.
     * Sorted: 80, 80, 80, 80, 200.  Median = 80 (outlier suppressed). */
    for (int i = 0; i < 5; i++) {
        fill_disparity (frame, W * H, (i == 2) ? 200 : 80);
        ag_temporal_filter_push (f, frame, output);
    }

    for (int i = 0; i < W * H; i++)
        TEST_ASSERT_EQUAL_INT16 (80, output[i]);

    ag_temporal_filter_destroy (f);
}

/* ------------------------------------------------------------------ */
/*  Tests: invalid pixel handling                                      */
/* ------------------------------------------------------------------ */

void test_invalid_pixels_skipped (void)
{
    enum { W = 4, H = 4, DEPTH = 3 };
    AgTemporalFilter *f = ag_temporal_filter_create (W, H, DEPTH);
    TEST_ASSERT_NOT_NULL (f);

    int16_t frame[W * H];
    int16_t output[W * H];

    /* Frame 0: all 80 except pixel 0 is invalid. */
    fill_disparity (frame, W * H, 80);
    frame[0] = INVALID_DISP;
    ag_temporal_filter_push (f, frame, output);

    /* Frame 1: all 100. */
    fill_disparity (frame, W * H, 100);
    ag_temporal_filter_push (f, frame, output);

    /* Frame 2: all 60. */
    fill_disparity (frame, W * H, 60);
    ag_temporal_filter_push (f, frame, output);

    /* Pixel 0 has only 2 valid values (100, 60) → median = average = 80. */
    TEST_ASSERT_EQUAL_INT16 (80, output[0]);

    /* Other pixels have 3 valid values (80, 100, 60) → median = 80. */
    TEST_ASSERT_EQUAL_INT16 (80, output[1]);

    ag_temporal_filter_destroy (f);
}

void test_all_invalid_stays_invalid (void)
{
    enum { W = 4, H = 4, DEPTH = 3 };
    AgTemporalFilter *f = ag_temporal_filter_create (W, H, DEPTH);
    TEST_ASSERT_NOT_NULL (f);

    int16_t frame[W * H];
    int16_t output[W * H];

    /* Push 3 frames where pixel 0 is always invalid. */
    for (int i = 0; i < 3; i++) {
        fill_disparity (frame, W * H, (int16_t) (50 + i * 20));
        frame[0] = INVALID_DISP;
        ag_temporal_filter_push (f, frame, output);
    }

    TEST_ASSERT_TRUE (output[0] <= 0);

    ag_temporal_filter_destroy (f);
}

/* ------------------------------------------------------------------ */
/*  Tests: ring buffer wrapping                                        */
/* ------------------------------------------------------------------ */

void test_ring_buffer_wrapping (void)
{
    enum { W = 4, H = 4, DEPTH = 3 };
    AgTemporalFilter *f = ag_temporal_filter_create (W, H, DEPTH);
    TEST_ASSERT_NOT_NULL (f);

    int16_t frame[W * H];
    int16_t output[W * H];

    /* Push 5 frames: 10, 20, 30, 40, 50.
     * After wrapping, buffer holds frames 30, 40, 50.
     * Sorted: 30, 40, 50.  Median = 40. */
    for (int i = 0; i < 5; i++) {
        fill_disparity (frame, W * H, (int16_t) ((i + 1) * 10));
        ag_temporal_filter_push (f, frame, output);
    }

    for (int i = 0; i < W * H; i++)
        TEST_ASSERT_EQUAL_INT16 (40, output[i]);

    ag_temporal_filter_destroy (f);
}

/* ------------------------------------------------------------------ */
/*  Tests: reset                                                       */
/* ------------------------------------------------------------------ */

void test_reset_clears_history (void)
{
    enum { W = 4, H = 4, DEPTH = 3 };
    AgTemporalFilter *f = ag_temporal_filter_create (W, H, DEPTH);
    TEST_ASSERT_NOT_NULL (f);

    int16_t frame[W * H];
    int16_t output[W * H];

    /* Fill buffer with value 80. */
    fill_disparity (frame, W * H, 80);
    for (int i = 0; i < 3; i++)
        ag_temporal_filter_push (f, frame, output);

    /* Reset and push a single frame with 200. */
    ag_temporal_filter_reset (f);

    fill_disparity (frame, W * H, 200);
    ag_temporal_filter_push (f, frame, output);

    /* Only 1 frame after reset → output should be 200 (not blended with 80). */
    for (int i = 0; i < W * H; i++)
        TEST_ASSERT_EQUAL_INT16 (200, output[i]);

    ag_temporal_filter_destroy (f);
}

/* ------------------------------------------------------------------ */
/*  Tests: in-place operation                                          */
/* ------------------------------------------------------------------ */

void test_inplace_operation (void)
{
    enum { W = 4, H = 4, DEPTH = 3 };
    AgTemporalFilter *f = ag_temporal_filter_create (W, H, DEPTH);
    TEST_ASSERT_NOT_NULL (f);

    int16_t buf[W * H];

    /* Push 3 frames in-place: 50, 100, 70 → median = 70. */
    fill_disparity (buf, W * H, 50);
    ag_temporal_filter_push (f, buf, buf);

    fill_disparity (buf, W * H, 100);
    ag_temporal_filter_push (f, buf, buf);

    fill_disparity (buf, W * H, 70);
    ag_temporal_filter_push (f, buf, buf);

    for (int i = 0; i < W * H; i++)
        TEST_ASSERT_EQUAL_INT16 (70, buf[i]);

    ag_temporal_filter_destroy (f);
}

/* ------------------------------------------------------------------ */
/*  Main                                                               */
/* ------------------------------------------------------------------ */

int
main (void)
{
    UNITY_BEGIN ();

    /* creation / destruction */
    RUN_TEST (test_create_destroy);
    RUN_TEST (test_create_invalid_args);
    RUN_TEST (test_destroy_null_safe);

    /* single frame */
    RUN_TEST (test_single_frame_passthrough);

    /* median computation */
    RUN_TEST (test_median_odd_depth);
    RUN_TEST (test_median_even_depth);
    RUN_TEST (test_median_suppresses_outlier);

    /* invalid pixels */
    RUN_TEST (test_invalid_pixels_skipped);
    RUN_TEST (test_all_invalid_stays_invalid);

    /* ring buffer */
    RUN_TEST (test_ring_buffer_wrapping);

    /* reset */
    RUN_TEST (test_reset_clears_history);

    /* in-place */
    RUN_TEST (test_inplace_operation);

    return UNITY_END ();
}
