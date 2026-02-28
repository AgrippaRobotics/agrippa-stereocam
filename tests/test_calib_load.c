/*
 * test_calib_load.c — unit tests for calib_load (shared calibration loader)
 *
 * Uses the sample calibration data at calibration/sample_calibration/.
 * No camera hardware is required (only tests local-path loading).
 *
 * Build:  make test
 * Run:    bin/test_calib_load [-s suite] [-t test]
 */

#include "../vendor/greatest.h"
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

/* ------------------------------------------------------------------ */
/*  Suite: calib_load_local                                            */
/* ------------------------------------------------------------------ */

TEST load_from_local_path (void)
{
    AgCalibSource src = { .local_path = SAMPLE_SESSION, .slot = -1 };
    AgRemapTable *left  = NULL;
    AgRemapTable *right = NULL;
    AgCalibMeta   meta  = {0};

    ASSERT_EQ (0, ag_calib_load (NULL, &src, &left, &right, &meta));
    ASSERT (left  != NULL);
    ASSERT (right != NULL);
    ASSERT_EQ (EXPECTED_WIDTH,  left->width);
    ASSERT_EQ (EXPECTED_HEIGHT, left->height);
    ASSERT_EQ (EXPECTED_WIDTH,  right->width);
    ASSERT_EQ (EXPECTED_HEIGHT, right->height);

    ag_remap_table_free (left);
    ag_remap_table_free (right);
    PASS ();
}

TEST load_local_metadata (void)
{
    AgCalibSource src = { .local_path = SAMPLE_SESSION, .slot = -1 };
    AgRemapTable *left  = NULL;
    AgRemapTable *right = NULL;
    AgCalibMeta   meta  = {0};

    ASSERT_EQ (0, ag_calib_load (NULL, &src, &left, &right, &meta));

    ASSERT_EQ (EXPECTED_MIN_DISP, meta.min_disparity);
    ASSERT_EQ (EXPECTED_NUM_DISP, meta.num_disparities);
    ASSERT (meta.focal_length_px > EXPECTED_FOCAL_LENGTH - EPSILON
         && meta.focal_length_px < EXPECTED_FOCAL_LENGTH + EPSILON);
    ASSERT (meta.baseline_cm > EXPECTED_BASELINE - EPSILON
         && meta.baseline_cm < EXPECTED_BASELINE + EPSILON);

    ag_remap_table_free (left);
    ag_remap_table_free (right);
    PASS ();
}

TEST load_local_null_meta (void)
{
    AgCalibSource src = { .local_path = SAMPLE_SESSION, .slot = -1 };
    AgRemapTable *left  = NULL;
    AgRemapTable *right = NULL;

    /* out_meta = NULL should be safe. */
    ASSERT_EQ (0, ag_calib_load (NULL, &src, &left, &right, NULL));
    ASSERT (left  != NULL);
    ASSERT (right != NULL);

    ag_remap_table_free (left);
    ag_remap_table_free (right);
    PASS ();
}

TEST load_nonexistent_path (void)
{
    AgCalibSource src = { .local_path = "/no/such/path", .slot = -1 };
    AgRemapTable *left  = NULL;
    AgRemapTable *right = NULL;

    int rc = ag_calib_load (NULL, &src, &left, &right, NULL);
    ASSERT (rc != 0);
    ASSERT (left  == NULL);
    ASSERT (right == NULL);
    PASS ();
}

TEST load_no_source (void)
{
    AgCalibSource src = { .local_path = NULL, .slot = -1 };
    AgRemapTable *left  = NULL;
    AgRemapTable *right = NULL;

    int rc = ag_calib_load (NULL, &src, &left, &right, NULL);
    ASSERT (rc != 0);
    PASS ();
}

TEST load_remap_data_nonzero (void)
{
    AgCalibSource src = { .local_path = SAMPLE_SESSION, .slot = -1 };
    AgRemapTable *left  = NULL;
    AgRemapTable *right = NULL;

    ASSERT_EQ (0, ag_calib_load (NULL, &src, &left, &right, NULL));

    /* Verify remap data is non-trivial (not all zeros). */
    size_t n = (size_t) left->width * left->height;
    int found_nonzero = 0;
    for (size_t i = 0; i < n && !found_nonzero; i++)
        if (left->offsets[i] != 0)
            found_nonzero = 1;
    ASSERT (found_nonzero);

    found_nonzero = 0;
    for (size_t i = 0; i < n && !found_nonzero; i++)
        if (right->offsets[i] != 0)
            found_nonzero = 1;
    ASSERT (found_nonzero);

    ag_remap_table_free (left);
    ag_remap_table_free (right);
    PASS ();
}

SUITE (calib_load_local)
{
    RUN_TEST (load_from_local_path);
    RUN_TEST (load_local_metadata);
    RUN_TEST (load_local_null_meta);
    RUN_TEST (load_nonexistent_path);
    RUN_TEST (load_no_source);
    RUN_TEST (load_remap_data_nonzero);
}

/* ------------------------------------------------------------------ */
/*  Suite: calib_load_meta                                             */
/* ------------------------------------------------------------------ */

TEST meta_parse_fields (void)
{
    AgCalibMeta meta = {0};
    ASSERT_EQ (0, ag_calib_load_meta (SAMPLE_SESSION, &meta));

    ASSERT_EQ (EXPECTED_MIN_DISP, meta.min_disparity);
    ASSERT_EQ (EXPECTED_NUM_DISP, meta.num_disparities);
    ASSERT (meta.focal_length_px > EXPECTED_FOCAL_LENGTH - EPSILON
         && meta.focal_length_px < EXPECTED_FOCAL_LENGTH + EPSILON);
    ASSERT (meta.baseline_cm > EXPECTED_BASELINE - EPSILON
         && meta.baseline_cm < EXPECTED_BASELINE + EPSILON);
    PASS ();
}

TEST meta_nonexistent_path (void)
{
    AgCalibMeta meta = {0};
    int rc = ag_calib_load_meta ("/no/such/path", &meta);
    ASSERT (rc != 0);
    PASS ();
}

TEST meta_fields_independent (void)
{
    /* Load metadata via ag_calib_load_meta and via ag_calib_load;
     * both should produce the same values. */
    AgCalibMeta meta_standalone = {0};
    ASSERT_EQ (0, ag_calib_load_meta (SAMPLE_SESSION, &meta_standalone));

    AgCalibSource src = { .local_path = SAMPLE_SESSION, .slot = -1 };
    AgRemapTable *left = NULL, *right = NULL;
    AgCalibMeta meta_combined = {0};
    ASSERT_EQ (0, ag_calib_load (NULL, &src, &left, &right, &meta_combined));

    ASSERT_EQ (meta_standalone.min_disparity,  meta_combined.min_disparity);
    ASSERT_EQ (meta_standalone.num_disparities, meta_combined.num_disparities);
    ASSERT (meta_standalone.focal_length_px > meta_combined.focal_length_px - EPSILON
         && meta_standalone.focal_length_px < meta_combined.focal_length_px + EPSILON);
    ASSERT (meta_standalone.baseline_cm > meta_combined.baseline_cm - EPSILON
         && meta_standalone.baseline_cm < meta_combined.baseline_cm + EPSILON);

    ag_remap_table_free (left);
    ag_remap_table_free (right);
    PASS ();
}

SUITE (calib_load_meta)
{
    RUN_TEST (meta_parse_fields);
    RUN_TEST (meta_nonexistent_path);
    RUN_TEST (meta_fields_independent);
}

/* ------------------------------------------------------------------ */
/*  Main                                                               */
/* ------------------------------------------------------------------ */

GREATEST_MAIN_DEFS ();

int
main (int argc, char *argv[])
{
    GREATEST_MAIN_BEGIN ();

    RUN_SUITE (calib_load_local);
    RUN_SUITE (calib_load_meta);

    GREATEST_MAIN_END ();
}
