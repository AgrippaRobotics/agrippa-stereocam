/*
 * test_npy.c — unit tests for npy.h / npy.c
 *
 * Uses sample .npy files from calibration/sample_calibration/calib_result/.
 */

#include "../vendor/unity/unity.h"
#include "npy.h"

#include <string.h>

#define SAMPLE_DIR  "calibration/sample_calibration/calib_result"

void setUp (void) {}
void tearDown (void) {}

/* Helper: load a .npy file from disk into a buffer. */
static int
load_file (const char *path, uint8_t **out_data, size_t *out_len)
{
    gchar  *contents = NULL;
    gsize   length   = 0;
    GError *err      = NULL;

    if (!g_file_get_contents (path, &contents, &length, &err)) {
        g_clear_error (&err);
        return -1;
    }

    *out_data = (uint8_t *) contents;
    *out_len  = (size_t) length;
    return 0;
}

void test_cam_mats_shape (void)
{
    uint8_t *data = NULL;
    size_t   len  = 0;
    TEST_ASSERT_EQUAL_INT (0, load_file (SAMPLE_DIR "/cam_mats_left.npy",
                                          &data, &len));

    AgNpyArray arr;
    TEST_ASSERT_EQUAL_INT (0, ag_npy_load (data, len, &arr));

    TEST_ASSERT_EQUAL_INT (2, arr.ndim);
    TEST_ASSERT_EQUAL_INT (3, arr.shape[0]);
    TEST_ASSERT_EQUAL_INT (3, arr.shape[1]);
    TEST_ASSERT_EQUAL_size_t (9, arr.n_elements);

    /* fx should be a large positive value (~875 px). */
    TEST_ASSERT_TRUE (arr.data[0] > 100.0);

    g_free (arr.data);
    g_free (data);
}

void test_dist_coefs_shape (void)
{
    uint8_t *data = NULL;
    size_t   len  = 0;
    TEST_ASSERT_EQUAL_INT (0, load_file (SAMPLE_DIR "/dist_coefs_left.npy",
                                          &data, &len));

    AgNpyArray arr;
    TEST_ASSERT_EQUAL_INT (0, ag_npy_load (data, len, &arr));

    TEST_ASSERT_EQUAL_INT (2, arr.ndim);
    TEST_ASSERT_EQUAL_INT (1, arr.shape[0]);
    TEST_ASSERT_EQUAL_INT (14, arr.shape[1]);
    TEST_ASSERT_EQUAL_size_t (14, arr.n_elements);

    g_free (arr.data);
    g_free (data);
}

void test_proj_mats_shape (void)
{
    uint8_t *data = NULL;
    size_t   len  = 0;
    TEST_ASSERT_EQUAL_INT (0, load_file (SAMPLE_DIR "/proj_mats_left.npy",
                                          &data, &len));

    AgNpyArray arr;
    TEST_ASSERT_EQUAL_INT (0, ag_npy_load (data, len, &arr));

    TEST_ASSERT_EQUAL_INT (2, arr.ndim);
    TEST_ASSERT_EQUAL_INT (3, arr.shape[0]);
    TEST_ASSERT_EQUAL_INT (4, arr.shape[1]);
    TEST_ASSERT_EQUAL_size_t (12, arr.n_elements);

    g_free (arr.data);
    g_free (data);
}

void test_rect_trans_shape (void)
{
    uint8_t *data = NULL;
    size_t   len  = 0;
    TEST_ASSERT_EQUAL_INT (0, load_file (SAMPLE_DIR "/rect_trans_left.npy",
                                          &data, &len));

    AgNpyArray arr;
    TEST_ASSERT_EQUAL_INT (0, ag_npy_load (data, len, &arr));

    TEST_ASSERT_EQUAL_INT (2, arr.ndim);
    TEST_ASSERT_EQUAL_INT (3, arr.shape[0]);
    TEST_ASSERT_EQUAL_INT (3, arr.shape[1]);
    TEST_ASSERT_EQUAL_size_t (9, arr.n_elements);

    g_free (arr.data);
    g_free (data);
}

void test_bad_magic (void)
{
    uint8_t bad[16] = { 0 };
    AgNpyArray arr;
    TEST_ASSERT_EQUAL_INT (-1, ag_npy_load (bad, sizeof bad, &arr));
}

void test_truncated (void)
{
    uint8_t *data = NULL;
    size_t   len  = 0;
    TEST_ASSERT_EQUAL_INT (0, load_file (SAMPLE_DIR "/cam_mats_left.npy",
                                          &data, &len));

    /* Truncate to just the header (no data). */
    AgNpyArray arr;
    TEST_ASSERT_EQUAL_INT (-1, ag_npy_load (data, 64, &arr));

    g_free (data);
}

int main (void)
{
    UNITY_BEGIN ();
    RUN_TEST (test_cam_mats_shape);
    RUN_TEST (test_dist_coefs_shape);
    RUN_TEST (test_proj_mats_shape);
    RUN_TEST (test_rect_trans_shape);
    RUN_TEST (test_bad_magic);
    RUN_TEST (test_truncated);
    return UNITY_END ();
}
