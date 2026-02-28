/*
 * test_calib_load_slot.c — unit tests for the on-camera slot loading
 *                           path in calib_load.c
 *
 * This is the first test file written with the Unity test framework.
 * It exercises the code path: ag_calib_load (slot >= 0) →
 *   ag_device_file_read → ag_multislot_extract_slot →
 *   ag_calib_archive_unpack.
 *
 * Uses mock_device_file.c to inject archive data without a camera.
 *
 * No camera hardware is required.
 *
 * Build:  make test
 * Run:    bin/test_calib_load_slot [-v]
 */

#include "../vendor/unity/unity.h"
#include "calib_load.h"
#include "calib_archive.h"
#include "mock_device_file.h"

#include <string.h>

#define SAMPLE_SESSION  "calibration/sample_calibration"

/* ------------------------------------------------------------------ */
/*  Shared fixtures                                                    */
/* ------------------------------------------------------------------ */

/*
 * A packed AGST archive built from the sample calibration.
 * Created once in main() before tests run, freed after.
 */
static uint8_t *g_packed_agst  = NULL;
static size_t   g_packed_len   = 0;

/*
 * A multi-slot AGMS container with slots 0 and 2 populated.
 */
static uint8_t *g_agms_data = NULL;
static size_t   g_agms_len  = 0;

/* ------------------------------------------------------------------ */
/*  Unity setUp / tearDown                                             */
/* ------------------------------------------------------------------ */

void setUp (void)
{
    mock_device_file_reset ();
}

void tearDown (void)
{
    /* nothing — mock_device_file_reset handles cleanup */
}

/* ------------------------------------------------------------------ */
/*  Tests: slot loading with a legacy single-slot AGST blob            */
/* ------------------------------------------------------------------ */

void test_slot_load_legacy_agst (void)
{
    /* Inject the AGST blob directly — legacy single-slot format.
     * Slot 0 should succeed. */
    mock_device_file_set_read_data (g_packed_agst, g_packed_len);

    AgCalibSource src = { .local_path = NULL, .slot = 0 };
    AgRemapTable *left  = NULL;
    AgRemapTable *right = NULL;
    AgCalibMeta   meta  = {0};

    TEST_ASSERT_EQUAL_INT (0, ag_calib_load (NULL, &src, &left, &right, &meta));
    TEST_ASSERT_NOT_NULL (left);
    TEST_ASSERT_NOT_NULL (right);

    TEST_ASSERT_EQUAL_UINT32 (1440, left->width);
    TEST_ASSERT_EQUAL_UINT32 (1080, left->height);
    TEST_ASSERT_EQUAL_UINT32 (1440, right->width);
    TEST_ASSERT_EQUAL_UINT32 (1080, right->height);

    TEST_ASSERT_EQUAL_INT (1, mock_device_file_read_call_count ());

    ag_remap_table_free (left);
    ag_remap_table_free (right);
}

void test_slot_load_legacy_metadata (void)
{
    mock_device_file_set_read_data (g_packed_agst, g_packed_len);

    AgCalibSource src = { .local_path = NULL, .slot = 0 };
    AgRemapTable *left  = NULL;
    AgRemapTable *right = NULL;
    AgCalibMeta   meta  = {0};

    TEST_ASSERT_EQUAL_INT (0, ag_calib_load (NULL, &src, &left, &right, &meta));

    TEST_ASSERT_EQUAL_INT (17,  meta.min_disparity);
    TEST_ASSERT_EQUAL_INT (128, meta.num_disparities);
    TEST_ASSERT_FLOAT_WITHIN (0.01, 875.24, meta.focal_length_px);
    TEST_ASSERT_FLOAT_WITHIN (0.01, 4.0677, meta.baseline_cm);

    ag_remap_table_free (left);
    ag_remap_table_free (right);
}

void test_slot_load_legacy_null_meta (void)
{
    mock_device_file_set_read_data (g_packed_agst, g_packed_len);

    AgCalibSource src = { .local_path = NULL, .slot = 0 };
    AgRemapTable *left  = NULL;
    AgRemapTable *right = NULL;

    /* out_meta = NULL should be safe. */
    TEST_ASSERT_EQUAL_INT (0, ag_calib_load (NULL, &src, &left, &right, NULL));
    TEST_ASSERT_NOT_NULL (left);
    TEST_ASSERT_NOT_NULL (right);

    ag_remap_table_free (left);
    ag_remap_table_free (right);
}

void test_slot_load_legacy_slot1_fails (void)
{
    /* Legacy AGST only has slot 0.  Slot 1 should fail. */
    mock_device_file_set_read_data (g_packed_agst, g_packed_len);

    AgCalibSource src = { .local_path = NULL, .slot = 1 };
    AgRemapTable *left  = NULL;
    AgRemapTable *right = NULL;

    TEST_ASSERT_NOT_EQUAL (0, ag_calib_load (NULL, &src, &left, &right, NULL));
    TEST_ASSERT_NULL (left);
    TEST_ASSERT_NULL (right);
}

/* ------------------------------------------------------------------ */
/*  Tests: slot loading from multi-slot AGMS container                 */
/* ------------------------------------------------------------------ */

void test_multislot_load_slot0 (void)
{
    mock_device_file_set_read_data (g_agms_data, g_agms_len);

    AgCalibSource src = { .local_path = NULL, .slot = 0 };
    AgRemapTable *left  = NULL;
    AgRemapTable *right = NULL;
    AgCalibMeta   meta  = {0};

    TEST_ASSERT_EQUAL_INT (0, ag_calib_load (NULL, &src, &left, &right, &meta));
    TEST_ASSERT_NOT_NULL (left);
    TEST_ASSERT_NOT_NULL (right);
    TEST_ASSERT_EQUAL_UINT32 (1440, left->width);

    TEST_ASSERT_EQUAL_INT (17,  meta.min_disparity);
    TEST_ASSERT_EQUAL_INT (128, meta.num_disparities);

    ag_remap_table_free (left);
    ag_remap_table_free (right);
}

void test_multislot_load_slot2 (void)
{
    mock_device_file_set_read_data (g_agms_data, g_agms_len);

    AgCalibSource src = { .local_path = NULL, .slot = 2 };
    AgRemapTable *left  = NULL;
    AgRemapTable *right = NULL;

    TEST_ASSERT_EQUAL_INT (0, ag_calib_load (NULL, &src, &left, &right, NULL));
    TEST_ASSERT_NOT_NULL (left);
    TEST_ASSERT_NOT_NULL (right);
    TEST_ASSERT_EQUAL_UINT32 (1440, left->width);

    ag_remap_table_free (left);
    ag_remap_table_free (right);
}

void test_multislot_empty_slot1_fails (void)
{
    /* Slot 1 was not populated in our AGMS fixture. */
    mock_device_file_set_read_data (g_agms_data, g_agms_len);

    AgCalibSource src = { .local_path = NULL, .slot = 1 };
    AgRemapTable *left  = NULL;
    AgRemapTable *right = NULL;

    TEST_ASSERT_NOT_EQUAL (0, ag_calib_load (NULL, &src, &left, &right, NULL));
    TEST_ASSERT_NULL (left);
    TEST_ASSERT_NULL (right);
}

/* ------------------------------------------------------------------ */
/*  Tests: error paths                                                 */
/* ------------------------------------------------------------------ */

void test_device_read_failure (void)
{
    mock_device_file_set_read_rc (-1);

    AgCalibSource src = { .local_path = NULL, .slot = 0 };
    AgRemapTable *left  = NULL;
    AgRemapTable *right = NULL;

    TEST_ASSERT_NOT_EQUAL (0, ag_calib_load (NULL, &src, &left, &right, NULL));
    TEST_ASSERT_NULL (left);
    TEST_ASSERT_NULL (right);
    TEST_ASSERT_EQUAL_INT (1, mock_device_file_read_call_count ());
}

void test_corrupt_archive_data (void)
{
    uint8_t garbage[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x00, 0x00, 0x00 };
    mock_device_file_set_read_data (garbage, sizeof (garbage));

    AgCalibSource src = { .local_path = NULL, .slot = 0 };
    AgRemapTable *left  = NULL;
    AgRemapTable *right = NULL;

    TEST_ASSERT_NOT_EQUAL (0, ag_calib_load (NULL, &src, &left, &right, NULL));
    TEST_ASSERT_NULL (left);
    TEST_ASSERT_NULL (right);
}

void test_truncated_archive (void)
{
    /* Give only the first 64 bytes of a valid archive — enough for
     * magic detection but not enough for a complete unpack. */
    size_t trunc_len = 64;
    if (trunc_len > g_packed_len)
        trunc_len = g_packed_len;

    mock_device_file_set_read_data (g_packed_agst, trunc_len);

    AgCalibSource src = { .local_path = NULL, .slot = 0 };
    AgRemapTable *left  = NULL;
    AgRemapTable *right = NULL;

    TEST_ASSERT_NOT_EQUAL (0, ag_calib_load (NULL, &src, &left, &right, NULL));
    TEST_ASSERT_NULL (left);
    TEST_ASSERT_NULL (right);
}

/* ------------------------------------------------------------------ */
/*  Main                                                               */
/* ------------------------------------------------------------------ */

int
main (void)
{
    /* Build test fixtures from sample calibration. */
    if (ag_calib_archive_pack (SAMPLE_SESSION,
                                &g_packed_agst, &g_packed_len) != 0) {
        fprintf (stderr, "FATAL: cannot pack sample calibration\n");
        return 1;
    }

    /* Build a multi-slot container: slot 0 and slot 2 populated. */
    uint8_t *agms_tmp = NULL;
    size_t   agms_tmp_len = 0;

    /* Start with empty → add slot 0. */
    if (ag_multislot_build (NULL, 0, 0,
                             g_packed_agst, g_packed_len,
                             &agms_tmp, &agms_tmp_len) != 0) {
        fprintf (stderr, "FATAL: multislot_build slot 0 failed\n");
        return 1;
    }

    /* Add slot 2 (slot 1 stays empty). */
    if (ag_multislot_build (agms_tmp, agms_tmp_len, 2,
                             g_packed_agst, g_packed_len,
                             &g_agms_data, &g_agms_len) != 0) {
        fprintf (stderr, "FATAL: multislot_build slot 2 failed\n");
        g_free (agms_tmp);
        return 1;
    }
    g_free (agms_tmp);

    /* Run tests. */
    UNITY_BEGIN ();

    /* Legacy single-slot AGST loading. */
    RUN_TEST (test_slot_load_legacy_agst);
    RUN_TEST (test_slot_load_legacy_metadata);
    RUN_TEST (test_slot_load_legacy_null_meta);
    RUN_TEST (test_slot_load_legacy_slot1_fails);

    /* Multi-slot AGMS loading. */
    RUN_TEST (test_multislot_load_slot0);
    RUN_TEST (test_multislot_load_slot2);
    RUN_TEST (test_multislot_empty_slot1_fails);

    /* Error paths. */
    RUN_TEST (test_device_read_failure);
    RUN_TEST (test_corrupt_archive_data);
    RUN_TEST (test_truncated_archive);

    int result = UNITY_END ();

    /* Cleanup fixtures. */
    g_free (g_packed_agst);
    g_free (g_agms_data);

    return result;
}
