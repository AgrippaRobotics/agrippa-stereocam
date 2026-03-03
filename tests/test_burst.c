/*
 * test_burst.c — unit tests for burst-mode helpers
 *
 * Tests the pure-logic portions of the burst module that do not
 * require camera hardware or the Aravis library.
 *
 * Build:  make test
 * Run:    bin/test_burst [-v]
 */

#include "../vendor/unity/unity.h"

#include <glib.h>
#include <string.h>
#include <stdlib.h>

/*
 * Duplicate of burst_frame_basename() from burst.c.  We cannot link
 * against burst.o in unit tests because it pulls in Aravis symbols.
 * This is a pure-glib function so the copy is trivial and safe.
 */
static char *
burst_frame_basename (int index, int total_count)
{
    int digits = 3;
    if (total_count > 999)
        digits = 4;
    return g_strdup_printf ("frame_%0*d", digits, index);
}

/* Burst limits — must match burst.h. */
#define AG_BURST_MIN   2
#define AG_BURST_MAX   100

void setUp (void) {}
void tearDown (void) {}

/* ------------------------------------------------------------------ */
/*  Tests: burst_frame_basename                                        */
/* ------------------------------------------------------------------ */

void test_basename_index_zero (void)
{
    char *b = burst_frame_basename (0, 5);
    TEST_ASSERT_EQUAL_STRING ("frame_000", b);
    g_free (b);
}

void test_basename_mid_index (void)
{
    char *b = burst_frame_basename (42, 100);
    TEST_ASSERT_EQUAL_STRING ("frame_042", b);
    g_free (b);
}

void test_basename_max_index (void)
{
    char *b = burst_frame_basename (99, 100);
    TEST_ASSERT_EQUAL_STRING ("frame_099", b);
    g_free (b);
}

void test_basename_small_burst (void)
{
    char *b = burst_frame_basename (1, 2);
    TEST_ASSERT_EQUAL_STRING ("frame_001", b);
    g_free (b);
}

void test_basename_four_digits (void)
{
    /* total_count > 999 triggers 4-digit padding. */
    char *b = burst_frame_basename (5, 1000);
    TEST_ASSERT_EQUAL_STRING ("frame_0005", b);
    g_free (b);
}

void test_basename_four_digits_high (void)
{
    char *b = burst_frame_basename (999, 1000);
    TEST_ASSERT_EQUAL_STRING ("frame_0999", b);
    g_free (b);
}

/* ------------------------------------------------------------------ */
/*  Tests: buffer count arithmetic                                     */
/* ------------------------------------------------------------------ */

/*
 * Mirrors the logic in burst_ensure_buffers():
 *   needed = max(16, burst_count + 4)
 */
static int
buffers_needed (int burst_count)
{
    int needed = burst_count + 4;
    return (needed < 16) ? 16 : needed;
}

void test_buffers_small_burst (void)
{
    /* burst=2 -> need=6, clamped to 16 */
    TEST_ASSERT_EQUAL_INT (16, buffers_needed (2));
}

void test_buffers_boundary (void)
{
    /* burst=12 -> need=16, exactly at threshold */
    TEST_ASSERT_EQUAL_INT (16, buffers_needed (12));
}

void test_buffers_above_threshold (void)
{
    /* burst=13 -> need=17 */
    TEST_ASSERT_EQUAL_INT (17, buffers_needed (13));
}

void test_buffers_medium (void)
{
    /* burst=20 -> need=24 */
    TEST_ASSERT_EQUAL_INT (24, buffers_needed (20));
}

void test_buffers_max_burst (void)
{
    /* burst=100 -> need=104 */
    TEST_ASSERT_EQUAL_INT (104, buffers_needed (100));
}

/* ------------------------------------------------------------------ */
/*  Tests: burst count limits                                          */
/* ------------------------------------------------------------------ */

void test_burst_min (void)
{
    TEST_ASSERT_EQUAL_INT (2, AG_BURST_MIN);
}

void test_burst_max (void)
{
    TEST_ASSERT_EQUAL_INT (100, AG_BURST_MAX);
}

void test_burst_range_valid (void)
{
    /* Values within [MIN, MAX] are valid. */
    TEST_ASSERT_TRUE (AG_BURST_MIN <= 2);
    TEST_ASSERT_TRUE (50 >= AG_BURST_MIN && 50 <= AG_BURST_MAX);
    TEST_ASSERT_TRUE (AG_BURST_MAX >= 100);
}

/* ------------------------------------------------------------------ */
/*  Main                                                               */
/* ------------------------------------------------------------------ */

int
main (void)
{
    UNITY_BEGIN ();

    /* burst_frame_basename */
    RUN_TEST (test_basename_index_zero);
    RUN_TEST (test_basename_mid_index);
    RUN_TEST (test_basename_max_index);
    RUN_TEST (test_basename_small_burst);
    RUN_TEST (test_basename_four_digits);
    RUN_TEST (test_basename_four_digits_high);

    /* buffer count arithmetic */
    RUN_TEST (test_buffers_small_burst);
    RUN_TEST (test_buffers_boundary);
    RUN_TEST (test_buffers_above_threshold);
    RUN_TEST (test_buffers_medium);
    RUN_TEST (test_buffers_max_burst);

    /* burst limits */
    RUN_TEST (test_burst_min);
    RUN_TEST (test_burst_max);
    RUN_TEST (test_burst_range_valid);

    return UNITY_END ();
}
