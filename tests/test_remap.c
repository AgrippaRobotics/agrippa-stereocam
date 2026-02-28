/*
 * test_remap.c — unit tests for remap table loading and application
 *
 * Uses the sample remap data at calibration/sample_calibration/.
 * No camera hardware is required.
 *
 * Build:  make test
 * Run:    bin/test_remap [-v]
 */

#include "../vendor/unity/unity.h"
#include "remap.h"

#include <string.h>

#define SAMPLE_LEFT   "calibration/sample_calibration/calib_result/remap_left.bin"
#define SAMPLE_RIGHT  "calibration/sample_calibration/calib_result/remap_right.bin"

#define EXPECTED_WIDTH   1440
#define EXPECTED_HEIGHT  1080

void setUp (void) {}
void tearDown (void) {}

/* ------------------------------------------------------------------ */
/*  Tests: remap_load_file                                             */
/* ------------------------------------------------------------------ */

void test_load_left_remap (void)
{
    AgRemapTable *t = ag_remap_table_load (SAMPLE_LEFT);
    TEST_ASSERT_NOT_NULL (t);
    TEST_ASSERT_EQUAL_UINT32 (EXPECTED_WIDTH,  t->width);
    TEST_ASSERT_EQUAL_UINT32 (EXPECTED_HEIGHT, t->height);
    TEST_ASSERT_NOT_NULL (t->offsets);
    ag_remap_table_free (t);
}

void test_load_right_remap (void)
{
    AgRemapTable *t = ag_remap_table_load (SAMPLE_RIGHT);
    TEST_ASSERT_NOT_NULL (t);
    TEST_ASSERT_EQUAL_UINT32 (EXPECTED_WIDTH,  t->width);
    TEST_ASSERT_EQUAL_UINT32 (EXPECTED_HEIGHT, t->height);
    TEST_ASSERT_NOT_NULL (t->offsets);
    ag_remap_table_free (t);
}

void test_load_nonexistent (void)
{
    AgRemapTable *t = ag_remap_table_load ("/no/such/file.bin");
    TEST_ASSERT_NULL (t);
}

void test_free_null_safe (void)
{
    ag_remap_table_free (NULL);   /* must not crash */
}

/* ------------------------------------------------------------------ */
/*  Tests: remap_load_from_memory                                      */
/* ------------------------------------------------------------------ */

/*
 * Helper: read a file into a g_malloc'd buffer.
 */
static uint8_t *
slurp_file (const char *path, size_t *out_len)
{
    gchar  *contents = NULL;
    gsize   length   = 0;
    GError *err      = NULL;

    if (!g_file_get_contents (path, &contents, &length, &err)) {
        g_clear_error (&err);
        return NULL;
    }

    *out_len = (size_t) length;
    return (uint8_t *) contents;
}

void test_from_memory_matches_file (void)
{
    /* Load via file path. */
    AgRemapTable *file_tab = ag_remap_table_load (SAMPLE_LEFT);
    TEST_ASSERT_NOT_NULL (file_tab);

    /* Load via memory. */
    size_t   buf_len = 0;
    uint8_t *buf = slurp_file (SAMPLE_LEFT, &buf_len);
    TEST_ASSERT_NOT_NULL (buf);

    AgRemapTable *mem_tab = ag_remap_table_load_from_memory (buf, buf_len);
    TEST_ASSERT_NOT_NULL (mem_tab);

    /* Same dimensions. */
    TEST_ASSERT_EQUAL_UINT32 (file_tab->width,  mem_tab->width);
    TEST_ASSERT_EQUAL_UINT32 (file_tab->height, mem_tab->height);

    /* Identical offset data. */
    size_t n = (size_t) file_tab->width * file_tab->height;
    TEST_ASSERT_EQUAL_MEMORY (file_tab->offsets, mem_tab->offsets,
                              n * sizeof (uint32_t));

    ag_remap_table_free (file_tab);
    ag_remap_table_free (mem_tab);
    g_free (buf);
}

void test_bad_magic_rejected (void)
{
    uint8_t buf[32] = {0};
    memcpy (buf, "XXXX", 4);
    /* width=4, height=1, flags=0 */
    uint32_t w = 4, h = 1;
    memcpy (buf + 4, &w, 4);
    memcpy (buf + 8, &h, 4);

    AgRemapTable *t = ag_remap_table_load_from_memory (buf, 32);
    TEST_ASSERT_NULL (t);
}

void test_truncated_header_rejected (void)
{
    uint8_t buf[8] = { 'R', 'M', 'A', 'P', 0, 0, 0, 0 };
    AgRemapTable *t = ag_remap_table_load_from_memory (buf, 8);
    TEST_ASSERT_NULL (t);
}

void test_truncated_data_rejected (void)
{
    /* Valid header: RMAP, 1440, 1080, flags=0, but no offset data. */
    uint8_t buf[16];
    memcpy (buf, "RMAP", 4);
    uint32_t vals[3] = { 1440, 1080, 0 };
    memcpy (buf + 4, vals, 12);

    AgRemapTable *t = ag_remap_table_load_from_memory (buf, 16);
    TEST_ASSERT_NULL (t);
}

/* ------------------------------------------------------------------ */
/*  Tests: remap_apply                                                 */
/* ------------------------------------------------------------------ */

/*
 * Build a small remap table in memory for unit tests.
 * Offsets array is g_malloc'd; caller must ag_remap_table_free().
 */
static AgRemapTable *
make_test_table (uint32_t w, uint32_t h, uint32_t fill)
{
    size_t n = (size_t) w * h;
    AgRemapTable *t = g_malloc (sizeof (AgRemapTable));
    t->width   = w;
    t->height  = h;
    t->offsets = g_malloc (n * sizeof (uint32_t));

    for (size_t i = 0; i < n; i++)
        t->offsets[i] = fill;

    return t;
}

void test_rgb_identity (void)
{
    uint32_t w = 4, h = 4;
    size_t n = (size_t) w * h;

    AgRemapTable *t = make_test_table (w, h, 0);
    /* Set up identity mapping: offsets[i] = i. */
    for (uint32_t i = 0; i < n; i++)
        t->offsets[i] = i;

    /* Fill source with known pattern. */
    guint8 *src = g_malloc (n * 3);
    for (size_t i = 0; i < n * 3; i++)
        src[i] = (guint8) (i & 0xFF);

    guint8 *dst = g_malloc0 (n * 3);
    ag_remap_rgb (t, src, dst);

    TEST_ASSERT_EQUAL_MEMORY (src, dst, n * 3);

    g_free (src);
    g_free (dst);
    ag_remap_table_free (t);
}

void test_rgb_sentinel_produces_black (void)
{
    uint32_t w = 4, h = 4;
    size_t n = (size_t) w * h;

    AgRemapTable *t = make_test_table (w, h, AG_REMAP_SENTINEL);

    guint8 *src = g_malloc (n * 3);
    memset (src, 0xAB, n * 3);   /* non-zero source */

    guint8 *dst = g_malloc (n * 3);
    memset (dst, 0xFF, n * 3);   /* fill with non-zero so we detect zeroing */

    ag_remap_rgb (t, src, dst);

    /* All output pixels must be (0, 0, 0). */
    for (size_t i = 0; i < n * 3; i++)
        TEST_ASSERT_EQUAL_UINT8 (0, dst[i]);

    g_free (src);
    g_free (dst);
    ag_remap_table_free (t);
}

void test_gray_identity (void)
{
    uint32_t w = 4, h = 4;
    size_t n = (size_t) w * h;

    AgRemapTable *t = make_test_table (w, h, 0);
    for (uint32_t i = 0; i < n; i++)
        t->offsets[i] = i;

    guint8 *src = g_malloc (n);
    for (size_t i = 0; i < n; i++)
        src[i] = (guint8) (i * 17);   /* some non-trivial pattern */

    guint8 *dst = g_malloc0 (n);
    ag_remap_gray (t, src, dst);

    TEST_ASSERT_EQUAL_MEMORY (src, dst, n);

    g_free (src);
    g_free (dst);
    ag_remap_table_free (t);
}

void test_gray_sentinel_produces_black (void)
{
    uint32_t w = 4, h = 4;
    size_t n = (size_t) w * h;

    AgRemapTable *t = make_test_table (w, h, AG_REMAP_SENTINEL);

    guint8 *src = g_malloc (n);
    memset (src, 0xCD, n);

    guint8 *dst = g_malloc (n);
    memset (dst, 0xFF, n);

    ag_remap_gray (t, src, dst);

    for (size_t i = 0; i < n; i++)
        TEST_ASSERT_EQUAL_UINT8 (0, dst[i]);

    g_free (src);
    g_free (dst);
    ag_remap_table_free (t);
}

/* ------------------------------------------------------------------ */
/*  Main                                                               */
/* ------------------------------------------------------------------ */

int
main (void)
{
    UNITY_BEGIN ();

    /* remap_load_file */
    RUN_TEST (test_load_left_remap);
    RUN_TEST (test_load_right_remap);
    RUN_TEST (test_load_nonexistent);
    RUN_TEST (test_free_null_safe);

    /* remap_load_from_memory */
    RUN_TEST (test_from_memory_matches_file);
    RUN_TEST (test_bad_magic_rejected);
    RUN_TEST (test_truncated_header_rejected);
    RUN_TEST (test_truncated_data_rejected);

    /* remap_apply */
    RUN_TEST (test_rgb_identity);
    RUN_TEST (test_rgb_sentinel_produces_black);
    RUN_TEST (test_gray_identity);
    RUN_TEST (test_gray_sentinel_produces_black);

    return UNITY_END ();
}
