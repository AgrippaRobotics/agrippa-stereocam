/*
 * test_remap_from_calib.c — verify remap tables computed from .npy matrices
 *                            match the reference .bin files
 *
 * Uses sample calibration data at calibration/sample_calibration/.
 */

#include "../vendor/unity/unity.h"
#include "npy.h"
#include "remap.h"
#include "remap_from_calib.h"

#define SAMPLE_DIR   "calibration/sample_calibration/calib_result"
#define SAMPLE_LEFT  SAMPLE_DIR "/remap_left.bin"
#define SAMPLE_RIGHT SAMPLE_DIR "/remap_right.bin"

#define IMAGE_WIDTH   1440
#define IMAGE_HEIGHT  1080

void setUp (void) {}
void tearDown (void) {}

/* Helper: load a .npy file and return its data array. */
static int
load_npy (const char *path, AgNpyArray *out)
{
    gchar  *contents = NULL;
    gsize   length   = 0;
    GError *err      = NULL;

    if (!g_file_get_contents (path, &contents, &length, &err)) {
        g_clear_error (&err);
        return -1;
    }

    int rc = ag_npy_load ((const uint8_t *) contents, (size_t) length, out);
    g_free (contents);
    return rc;
}

/*
 * Compute a remap table from the sample .npy matrices for one side.
 */
static AgRemapTable *
compute_remap (const char *side)
{
    char cam_path[256], dist_path[256], rect_path[256], proj_path[256];
    snprintf (cam_path,  sizeof cam_path,  SAMPLE_DIR "/cam_mats_%s.npy", side);
    snprintf (dist_path, sizeof dist_path, SAMPLE_DIR "/dist_coefs_%s.npy", side);
    snprintf (rect_path, sizeof rect_path, SAMPLE_DIR "/rect_trans_%s.npy", side);
    snprintf (proj_path, sizeof proj_path, SAMPLE_DIR "/proj_mats_%s.npy", side);

    AgNpyArray K, D, R, P;
    if (load_npy (cam_path, &K) != 0) return NULL;
    if (load_npy (dist_path, &D) != 0) { g_free (K.data); return NULL; }
    if (load_npy (rect_path, &R) != 0) { g_free (K.data); g_free (D.data); return NULL; }
    if (load_npy (proj_path, &P) != 0) { g_free (K.data); g_free (D.data); g_free (R.data); return NULL; }

    AgRemapTable *table = ag_remap_from_calib (
        K.data, D.data, (int) D.n_elements,
        R.data, P.data, IMAGE_WIDTH, IMAGE_HEIGHT);

    g_free (K.data);
    g_free (D.data);
    g_free (R.data);
    g_free (P.data);

    return table;
}

void test_remap_dimensions (void)
{
    AgRemapTable *table = compute_remap ("left");
    TEST_ASSERT_NOT_NULL (table);
    TEST_ASSERT_EQUAL_UINT32 (IMAGE_WIDTH,  table->width);
    TEST_ASSERT_EQUAL_UINT32 (IMAGE_HEIGHT, table->height);
    ag_remap_table_free (table);
}

void test_remap_has_valid_offsets (void)
{
    AgRemapTable *table = compute_remap ("left");
    TEST_ASSERT_NOT_NULL (table);

    size_t n = (size_t) table->width * table->height;
    size_t valid = 0;
    size_t sentinel = 0;

    for (size_t i = 0; i < n; i++) {
        if (table->offsets[i] == AG_REMAP_SENTINEL)
            sentinel++;
        else
            valid++;
    }

    /* Most pixels should have valid mappings. */
    TEST_ASSERT_TRUE (valid > n / 2);
    /* Sentinels only appear for border pixels that map outside the source
     * image.  Some calibrations may produce zero sentinels — that's fine. */
    printf ("  Valid: %zu, sentinel: %zu (of %zu)\n", valid, sentinel, n);

    ag_remap_table_free (table);
}

void test_remap_matches_reference_left (void)
{
    AgRemapTable *computed = compute_remap ("left");
    TEST_ASSERT_NOT_NULL (computed);

    AgRemapTable *reference = ag_remap_table_load (SAMPLE_LEFT);
    TEST_ASSERT_NOT_NULL (reference);

    TEST_ASSERT_EQUAL_UINT32 (reference->width,  computed->width);
    TEST_ASSERT_EQUAL_UINT32 (reference->height, computed->height);

    size_t n = (size_t) computed->width * computed->height;
    size_t exact = 0;
    size_t close = 0;   /* within ±1 pixel offset */
    size_t mismatch = 0;

    for (size_t i = 0; i < n; i++) {
        uint32_t c = computed->offsets[i];
        uint32_t r = reference->offsets[i];

        if (c == r) {
            exact++;
        } else if (c == AG_REMAP_SENTINEL || r == AG_REMAP_SENTINEL) {
            /* One is OOB, the other isn't — border pixel discrepancy. */
            close++;
        } else {
            /* Check if pixel coordinates differ by at most 1 in each axis. */
            int cx = (int) (c % computed->width);
            int cy = (int) (c / computed->width);
            int rx = (int) (r % reference->width);
            int ry = (int) (r / reference->width);
            int dx = abs (cx - rx);
            int dy = abs (cy - ry);

            if (dx <= 1 && dy <= 1)
                close++;
            else
                mismatch++;
        }
    }

    /* At least 99% of pixels should match exactly. */
    double exact_pct = (double) exact / (double) n * 100.0;
    printf ("  Exact matches: %.2f%% (%zu/%zu)\n", exact_pct, exact, n);
    printf ("  Close matches: %zu, mismatches: %zu\n", close, mismatch);
    TEST_ASSERT_TRUE (exact_pct > 99.0);

    /* No pixel should differ by more than ±1. */
    TEST_ASSERT_EQUAL_size_t (0, mismatch);

    ag_remap_table_free (computed);
    ag_remap_table_free (reference);
}

void test_remap_matches_reference_right (void)
{
    AgRemapTable *computed = compute_remap ("right");
    TEST_ASSERT_NOT_NULL (computed);

    AgRemapTable *reference = ag_remap_table_load (SAMPLE_RIGHT);
    TEST_ASSERT_NOT_NULL (reference);

    size_t n = (size_t) computed->width * computed->height;
    size_t exact = 0;
    size_t mismatch = 0;

    for (size_t i = 0; i < n; i++) {
        uint32_t c = computed->offsets[i];
        uint32_t r = reference->offsets[i];

        if (c == r) {
            exact++;
        } else if (c != AG_REMAP_SENTINEL && r != AG_REMAP_SENTINEL) {
            int cx = (int) (c % computed->width);
            int cy = (int) (c / computed->width);
            int rx = (int) (r % reference->width);
            int ry = (int) (r / reference->width);
            if (abs (cx - rx) > 1 || abs (cy - ry) > 1)
                mismatch++;
        }
    }

    double exact_pct = (double) exact / (double) n * 100.0;
    printf ("  Exact matches: %.2f%% (%zu/%zu)\n", exact_pct, exact, n);
    TEST_ASSERT_TRUE (exact_pct > 99.0);
    TEST_ASSERT_EQUAL_size_t (0, mismatch);

    ag_remap_table_free (computed);
    ag_remap_table_free (reference);
}

int main (void)
{
    UNITY_BEGIN ();
    RUN_TEST (test_remap_dimensions);
    RUN_TEST (test_remap_has_valid_offsets);
    RUN_TEST (test_remap_matches_reference_left);
    RUN_TEST (test_remap_matches_reference_right);
    return UNITY_END ();
}
