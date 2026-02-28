/*
 * test_calib_load.c — unit tests for calib_load (shared calibration loader)
 *
 * Uses the sample calibration data at calibration/sample_calibration/.
 * No camera hardware is required (only tests local-path loading).
 *
 * Build:  make test
 * Run:    bin/test_calib_load [-v]
 */

#include "../vendor/unity/unity.h"
#include "calib_load.h"

#include <string.h>

/*
 * Stub for ag_device_file_read — unit tests never exercise the
 * on-camera slot path, but the linker needs the symbol because
 * calib_load.o references it.
 */
int ag_device_file_read (ArvDevice *dev, const char *file_selector,
                         uint8_t **out_data, size_t *out_len)
{
    (void) dev; (void) file_selector;
    (void) out_data; (void) out_len;
    return -1;
}

#define SAMPLE_SESSION  "calibration/sample_calibration"

/* Expected values from calibration_meta.json. */
#define EXPECTED_WIDTH          1440
#define EXPECTED_HEIGHT         1080
#define EXPECTED_MIN_DISP       17
#define EXPECTED_NUM_DISP       128
#define EXPECTED_FOCAL_LENGTH   875.24
#define EXPECTED_BASELINE       4.0677
#define EPSILON                 0.01

void setUp (void) {}
void tearDown (void) {}

/* ------------------------------------------------------------------ */
/*  Tests: calib_load_local                                            */
/* ------------------------------------------------------------------ */

void test_load_from_local_path (void)
{
    AgCalibSource src = { .local_path = SAMPLE_SESSION, .slot = -1 };
    AgRemapTable *left  = NULL;
    AgRemapTable *right = NULL;
    AgCalibMeta   meta  = {0};

    TEST_ASSERT_EQUAL_INT (0, ag_calib_load (NULL, &src, &left, &right, &meta));
    TEST_ASSERT_NOT_NULL (left);
    TEST_ASSERT_NOT_NULL (right);
    TEST_ASSERT_EQUAL_UINT32 (EXPECTED_WIDTH,  left->width);
    TEST_ASSERT_EQUAL_UINT32 (EXPECTED_HEIGHT, left->height);
    TEST_ASSERT_EQUAL_UINT32 (EXPECTED_WIDTH,  right->width);
    TEST_ASSERT_EQUAL_UINT32 (EXPECTED_HEIGHT, right->height);

    ag_remap_table_free (left);
    ag_remap_table_free (right);
}

void test_load_local_metadata (void)
{
    AgCalibSource src = { .local_path = SAMPLE_SESSION, .slot = -1 };
    AgRemapTable *left  = NULL;
    AgRemapTable *right = NULL;
    AgCalibMeta   meta  = {0};

    TEST_ASSERT_EQUAL_INT (0, ag_calib_load (NULL, &src, &left, &right, &meta));

    TEST_ASSERT_EQUAL_INT (EXPECTED_MIN_DISP, meta.min_disparity);
    TEST_ASSERT_EQUAL_INT (EXPECTED_NUM_DISP, meta.num_disparities);
    TEST_ASSERT_FLOAT_WITHIN (EPSILON, EXPECTED_FOCAL_LENGTH, meta.focal_length_px);
    TEST_ASSERT_FLOAT_WITHIN (EPSILON, EXPECTED_BASELINE, meta.baseline_cm);

    ag_remap_table_free (left);
    ag_remap_table_free (right);
}

void test_load_local_null_meta (void)
{
    AgCalibSource src = { .local_path = SAMPLE_SESSION, .slot = -1 };
    AgRemapTable *left  = NULL;
    AgRemapTable *right = NULL;

    /* out_meta = NULL should be safe. */
    TEST_ASSERT_EQUAL_INT (0, ag_calib_load (NULL, &src, &left, &right, NULL));
    TEST_ASSERT_NOT_NULL (left);
    TEST_ASSERT_NOT_NULL (right);

    ag_remap_table_free (left);
    ag_remap_table_free (right);
}

void test_load_nonexistent_path (void)
{
    AgCalibSource src = { .local_path = "/no/such/path", .slot = -1 };
    AgRemapTable *left  = NULL;
    AgRemapTable *right = NULL;

    int rc = ag_calib_load (NULL, &src, &left, &right, NULL);
    TEST_ASSERT_NOT_EQUAL (0, rc);
    TEST_ASSERT_NULL (left);
    TEST_ASSERT_NULL (right);
}

void test_load_no_source (void)
{
    AgCalibSource src = { .local_path = NULL, .slot = -1 };
    AgRemapTable *left  = NULL;
    AgRemapTable *right = NULL;

    int rc = ag_calib_load (NULL, &src, &left, &right, NULL);
    TEST_ASSERT_NOT_EQUAL (0, rc);
}

void test_load_remap_data_nonzero (void)
{
    AgCalibSource src = { .local_path = SAMPLE_SESSION, .slot = -1 };
    AgRemapTable *left  = NULL;
    AgRemapTable *right = NULL;

    TEST_ASSERT_EQUAL_INT (0, ag_calib_load (NULL, &src, &left, &right, NULL));

    /* Verify remap data is non-trivial (not all zeros). */
    size_t n = (size_t) left->width * left->height;
    int found_nonzero = 0;
    for (size_t i = 0; i < n && !found_nonzero; i++)
        if (left->offsets[i] != 0)
            found_nonzero = 1;
    TEST_ASSERT_TRUE (found_nonzero);

    found_nonzero = 0;
    for (size_t i = 0; i < n && !found_nonzero; i++)
        if (right->offsets[i] != 0)
            found_nonzero = 1;
    TEST_ASSERT_TRUE (found_nonzero);

    ag_remap_table_free (left);
    ag_remap_table_free (right);
}

/* ------------------------------------------------------------------ */
/*  Tests: calib_load_meta                                             */
/* ------------------------------------------------------------------ */

void test_meta_parse_fields (void)
{
    AgCalibMeta meta = {0};
    TEST_ASSERT_EQUAL_INT (0, ag_calib_load_meta (SAMPLE_SESSION, &meta));

    TEST_ASSERT_EQUAL_INT (EXPECTED_MIN_DISP, meta.min_disparity);
    TEST_ASSERT_EQUAL_INT (EXPECTED_NUM_DISP, meta.num_disparities);
    TEST_ASSERT_FLOAT_WITHIN (EPSILON, EXPECTED_FOCAL_LENGTH, meta.focal_length_px);
    TEST_ASSERT_FLOAT_WITHIN (EPSILON, EXPECTED_BASELINE, meta.baseline_cm);
}

void test_meta_nonexistent_path (void)
{
    AgCalibMeta meta = {0};
    int rc = ag_calib_load_meta ("/no/such/path", &meta);
    TEST_ASSERT_NOT_EQUAL (0, rc);
}

void test_meta_fields_independent (void)
{
    /* Load metadata via ag_calib_load_meta and via ag_calib_load;
     * both should produce the same values. */
    AgCalibMeta meta_standalone = {0};
    TEST_ASSERT_EQUAL_INT (0, ag_calib_load_meta (SAMPLE_SESSION, &meta_standalone));

    AgCalibSource src = { .local_path = SAMPLE_SESSION, .slot = -1 };
    AgRemapTable *left = NULL, *right = NULL;
    AgCalibMeta meta_combined = {0};
    TEST_ASSERT_EQUAL_INT (0, ag_calib_load (NULL, &src, &left, &right, &meta_combined));

    TEST_ASSERT_EQUAL_INT (meta_standalone.min_disparity,  meta_combined.min_disparity);
    TEST_ASSERT_EQUAL_INT (meta_standalone.num_disparities, meta_combined.num_disparities);
    TEST_ASSERT_FLOAT_WITHIN (EPSILON, meta_standalone.focal_length_px,
                              meta_combined.focal_length_px);
    TEST_ASSERT_FLOAT_WITHIN (EPSILON, meta_standalone.baseline_cm,
                              meta_combined.baseline_cm);

    ag_remap_table_free (left);
    ag_remap_table_free (right);
}

/* ------------------------------------------------------------------ */
/*  Main                                                               */
/* ------------------------------------------------------------------ */

int
main (void)
{
    UNITY_BEGIN ();

    RUN_TEST (test_load_from_local_path);
    RUN_TEST (test_load_local_metadata);
    RUN_TEST (test_load_local_null_meta);
    RUN_TEST (test_load_nonexistent_path);
    RUN_TEST (test_load_no_source);
    RUN_TEST (test_load_remap_data_nonzero);

    RUN_TEST (test_meta_parse_fields);
    RUN_TEST (test_meta_nonexistent_path);
    RUN_TEST (test_meta_fields_independent);

    return UNITY_END ();
}
