/*
 * test_remap.c — unit tests for remap table loading and application
 *
 * Uses the sample remap data at calibration/sample_calibration/.
 * No camera hardware is required.
 *
 * Build:  make test
 * Run:    bin/test_remap [-s suite] [-t test]
 */

#include "../vendor/greatest.h"
#include "remap.h"

#include <string.h>

#define SAMPLE_LEFT   "calibration/sample_calibration/calib_result/remap_left.bin"
#define SAMPLE_RIGHT  "calibration/sample_calibration/calib_result/remap_right.bin"

#define EXPECTED_WIDTH   1440
#define EXPECTED_HEIGHT  1080

/* ------------------------------------------------------------------ */
/*  Suite: remap_load_file                                             */
/* ------------------------------------------------------------------ */

TEST load_left_remap (void)
{
    AgRemapTable *t = ag_remap_table_load (SAMPLE_LEFT);
    ASSERT (t != NULL);
    ASSERT_EQ (EXPECTED_WIDTH,  t->width);
    ASSERT_EQ (EXPECTED_HEIGHT, t->height);
    ASSERT (t->offsets != NULL);
    ag_remap_table_free (t);
    PASS ();
}

TEST load_right_remap (void)
{
    AgRemapTable *t = ag_remap_table_load (SAMPLE_RIGHT);
    ASSERT (t != NULL);
    ASSERT_EQ (EXPECTED_WIDTH,  t->width);
    ASSERT_EQ (EXPECTED_HEIGHT, t->height);
    ASSERT (t->offsets != NULL);
    ag_remap_table_free (t);
    PASS ();
}

TEST load_nonexistent (void)
{
    AgRemapTable *t = ag_remap_table_load ("/no/such/file.bin");
    ASSERT (t == NULL);
    PASS ();
}

TEST free_null_safe (void)
{
    ag_remap_table_free (NULL);   /* must not crash */
    PASS ();
}

SUITE (remap_load_file)
{
    RUN_TEST (load_left_remap);
    RUN_TEST (load_right_remap);
    RUN_TEST (load_nonexistent);
    RUN_TEST (free_null_safe);
}

/* ------------------------------------------------------------------ */
/*  Suite: remap_load_from_memory                                      */
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

TEST from_memory_matches_file (void)
{
    /* Load via file path. */
    AgRemapTable *file_tab = ag_remap_table_load (SAMPLE_LEFT);
    ASSERT (file_tab != NULL);

    /* Load via memory. */
    size_t   buf_len = 0;
    uint8_t *buf = slurp_file (SAMPLE_LEFT, &buf_len);
    ASSERT (buf != NULL);

    AgRemapTable *mem_tab = ag_remap_table_load_from_memory (buf, buf_len);
    ASSERT (mem_tab != NULL);

    /* Same dimensions. */
    ASSERT_EQ (file_tab->width,  mem_tab->width);
    ASSERT_EQ (file_tab->height, mem_tab->height);

    /* Identical offset data. */
    size_t n = (size_t) file_tab->width * file_tab->height;
    ASSERT_MEM_EQ (file_tab->offsets, mem_tab->offsets,
                   n * sizeof (uint32_t));

    ag_remap_table_free (file_tab);
    ag_remap_table_free (mem_tab);
    g_free (buf);
    PASS ();
}

TEST bad_magic_rejected (void)
{
    uint8_t buf[32] = {0};
    memcpy (buf, "XXXX", 4);
    /* width=4, height=1, flags=0 */
    uint32_t w = 4, h = 1;
    memcpy (buf + 4, &w, 4);
    memcpy (buf + 8, &h, 4);

    AgRemapTable *t = ag_remap_table_load_from_memory (buf, 32);
    ASSERT (t == NULL);
    PASS ();
}

TEST truncated_header_rejected (void)
{
    uint8_t buf[8] = { 'R', 'M', 'A', 'P', 0, 0, 0, 0 };
    AgRemapTable *t = ag_remap_table_load_from_memory (buf, 8);
    ASSERT (t == NULL);
    PASS ();
}

TEST truncated_data_rejected (void)
{
    /* Valid header: RMAP, 1440, 1080, flags=0, but no offset data. */
    uint8_t buf[16];
    memcpy (buf, "RMAP", 4);
    uint32_t vals[3] = { 1440, 1080, 0 };
    memcpy (buf + 4, vals, 12);

    AgRemapTable *t = ag_remap_table_load_from_memory (buf, 16);
    ASSERT (t == NULL);
    PASS ();
}

SUITE (remap_load_from_memory)
{
    RUN_TEST (from_memory_matches_file);
    RUN_TEST (bad_magic_rejected);
    RUN_TEST (truncated_header_rejected);
    RUN_TEST (truncated_data_rejected);
}

/* ------------------------------------------------------------------ */
/*  Suite: remap_apply                                                 */
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

TEST rgb_identity (void)
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

    ASSERT_MEM_EQ (src, dst, n * 3);

    g_free (src);
    g_free (dst);
    ag_remap_table_free (t);
    PASS ();
}

TEST rgb_sentinel_produces_black (void)
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
        ASSERT_EQ (0, dst[i]);

    g_free (src);
    g_free (dst);
    ag_remap_table_free (t);
    PASS ();
}

TEST gray_identity (void)
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

    ASSERT_MEM_EQ (src, dst, n);

    g_free (src);
    g_free (dst);
    ag_remap_table_free (t);
    PASS ();
}

TEST gray_sentinel_produces_black (void)
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
        ASSERT_EQ (0, dst[i]);

    g_free (src);
    g_free (dst);
    ag_remap_table_free (t);
    PASS ();
}

SUITE (remap_apply)
{
    RUN_TEST (rgb_identity);
    RUN_TEST (rgb_sentinel_produces_black);
    RUN_TEST (gray_identity);
    RUN_TEST (gray_sentinel_produces_black);
}

/* ------------------------------------------------------------------ */
/*  Main                                                               */
/* ------------------------------------------------------------------ */

GREATEST_MAIN_DEFS ();

int
main (int argc, char *argv[])
{
    GREATEST_MAIN_BEGIN ();

    RUN_SUITE (remap_load_file);
    RUN_SUITE (remap_load_from_memory);
    RUN_SUITE (remap_apply);

    GREATEST_MAIN_END ();
}
