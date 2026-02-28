/*
 * test_calib_archive.c — unit tests for calib_archive pack/unpack/list
 *
 * Uses the sample calibration data at calibration/sample_calibration/.
 * No camera hardware is required.
 *
 * Build:  make test
 * Run:    bin/test_calib_archive [-v]
 */

#include "../vendor/unity/unity.h"
#include "calib_archive.h"
#include "../vendor/cJSON.h"

#include <string.h>
#include <zlib.h>

#define SAMPLE_SESSION  "calibration/sample_calibration"
#define SAMPLE_LEFT     "calibration/sample_calibration/calib_result/remap_left.bin"
#define SAMPLE_RIGHT    "calibration/sample_calibration/calib_result/remap_right.bin"

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

/* Read a uint32 little-endian from a byte pointer. */
static uint32_t
read_u32 (const uint8_t *p)
{
    return (uint32_t) p[0]
         | ((uint32_t) p[1] << 8)
         | ((uint32_t) p[2] << 16)
         | ((uint32_t) p[3] << 24);
}

/* ------------------------------------------------------------------ */
/*  Tests: pack_unpack_roundtrip                                       */
/* ------------------------------------------------------------------ */

void test_pack_sample_session (void)
{
    uint8_t *data = NULL;
    size_t   len  = 0;

    TEST_ASSERT_EQUAL_INT (0, ag_calib_archive_pack (SAMPLE_SESSION, &data, &len));
    TEST_ASSERT_NOT_NULL (data);
    TEST_ASSERT_TRUE (len > 0);

    g_free (data);
}

void test_roundtrip_remap_dimensions (void)
{
    uint8_t *archive = NULL;
    size_t   archive_len = 0;
    TEST_ASSERT_EQUAL_INT (0, ag_calib_archive_pack (SAMPLE_SESSION, &archive, &archive_len));

    AgRemapTable *left  = NULL;
    AgRemapTable *right = NULL;
    AgCalibMeta   meta  = {0};

    TEST_ASSERT_EQUAL_INT (0, ag_calib_archive_unpack (archive, archive_len,
                                                        &left, &right, &meta));
    TEST_ASSERT_NOT_NULL (left);
    TEST_ASSERT_NOT_NULL (right);
    TEST_ASSERT_EQUAL_UINT32 (EXPECTED_WIDTH,  left->width);
    TEST_ASSERT_EQUAL_UINT32 (EXPECTED_HEIGHT, left->height);
    TEST_ASSERT_EQUAL_UINT32 (EXPECTED_WIDTH,  right->width);
    TEST_ASSERT_EQUAL_UINT32 (EXPECTED_HEIGHT, right->height);

    ag_remap_table_free (left);
    ag_remap_table_free (right);
    g_free (archive);
}

void test_roundtrip_metadata (void)
{
    uint8_t *archive = NULL;
    size_t   archive_len = 0;
    TEST_ASSERT_EQUAL_INT (0, ag_calib_archive_pack (SAMPLE_SESSION, &archive, &archive_len));

    AgRemapTable *left  = NULL;
    AgRemapTable *right = NULL;
    AgCalibMeta   meta  = {0};

    TEST_ASSERT_EQUAL_INT (0, ag_calib_archive_unpack (archive, archive_len,
                                                        &left, &right, &meta));

    TEST_ASSERT_EQUAL_INT (EXPECTED_MIN_DISP, meta.min_disparity);
    TEST_ASSERT_EQUAL_INT (EXPECTED_NUM_DISP, meta.num_disparities);
    TEST_ASSERT_FLOAT_WITHIN (EPSILON, EXPECTED_FOCAL_LENGTH, meta.focal_length_px);
    TEST_ASSERT_FLOAT_WITHIN (EPSILON, EXPECTED_BASELINE, meta.baseline_cm);

    ag_remap_table_free (left);
    ag_remap_table_free (right);
    g_free (archive);
}

void test_roundtrip_remap_data_integrity (void)
{
    /* Pack and unpack. */
    uint8_t *archive = NULL;
    size_t   archive_len = 0;
    TEST_ASSERT_EQUAL_INT (0, ag_calib_archive_pack (SAMPLE_SESSION, &archive, &archive_len));

    AgRemapTable *arch_left  = NULL;
    AgRemapTable *arch_right = NULL;
    AgCalibMeta   meta       = {0};
    TEST_ASSERT_EQUAL_INT (0, ag_calib_archive_unpack (archive, archive_len,
                                                        &arch_left, &arch_right, &meta));

    /* Load from disk for comparison. */
    AgRemapTable *disk_left  = ag_remap_table_load (SAMPLE_LEFT);
    AgRemapTable *disk_right = ag_remap_table_load (SAMPLE_RIGHT);
    TEST_ASSERT_NOT_NULL (disk_left);
    TEST_ASSERT_NOT_NULL (disk_right);

    size_t n = (size_t) EXPECTED_WIDTH * EXPECTED_HEIGHT;
    TEST_ASSERT_EQUAL_MEMORY (disk_left->offsets, arch_left->offsets,
                              n * sizeof (uint32_t));
    TEST_ASSERT_EQUAL_MEMORY (disk_right->offsets, arch_right->offsets,
                              n * sizeof (uint32_t));

    ag_remap_table_free (arch_left);
    ag_remap_table_free (arch_right);
    ag_remap_table_free (disk_left);
    ag_remap_table_free (disk_right);
    g_free (archive);
}

/* ------------------------------------------------------------------ */
/*  Tests: archive_format                                              */
/* ------------------------------------------------------------------ */

void test_output_is_agst_envelope (void)
{
    uint8_t *data = NULL;
    size_t   len  = 0;
    TEST_ASSERT_EQUAL_INT (0, ag_calib_archive_pack (SAMPLE_SESSION, &data, &len));

    /* First 4 bytes: AGST magic. */
    TEST_ASSERT_EQUAL_MEMORY ("AGST", data, 4);

    /* Bytes 4-7: header size = 4096. */
    uint32_t hdr_size = read_u32 (data + 4);
    TEST_ASSERT_EQUAL_UINT32 (4096u, hdr_size);

    /* Total length exceeds the header. */
    TEST_ASSERT_TRUE (len > 4096);

    g_free (data);
}

void test_agst_header_contains_valid_json (void)
{
    uint8_t *data = NULL;
    size_t   len  = 0;
    TEST_ASSERT_EQUAL_INT (0, ag_calib_archive_pack (SAMPLE_SESSION, &data, &len));

    /* The JSON starts at byte 8, null-terminated within 4096 bytes. */
    const char *json_str = (const char *) (data + 8);
    size_t json_len = strnlen (json_str, 4096 - 8);
    TEST_ASSERT_TRUE (json_len > 0);

    cJSON *root = cJSON_ParseWithLength (json_str, json_len);
    TEST_ASSERT_NOT_NULL (root);

    /* Check for expected keys. */
    TEST_ASSERT_NOT_NULL (cJSON_GetObjectItemCaseSensitive (root, "image_size"));
    TEST_ASSERT_NOT_NULL (cJSON_GetObjectItemCaseSensitive (root, "rms_stereo_px"));
    TEST_ASSERT_NOT_NULL (cJSON_GetObjectItemCaseSensitive (root, "baseline_cm"));
    TEST_ASSERT_NOT_NULL (cJSON_GetObjectItemCaseSensitive (root, "focal_length_px"));
    TEST_ASSERT_NOT_NULL (cJSON_GetObjectItemCaseSensitive (root, "packed_at"));

    cJSON_Delete (root);
    g_free (data);
}

void test_agcz_payload_at_offset_4096 (void)
{
    uint8_t *data = NULL;
    size_t   len  = 0;
    TEST_ASSERT_EQUAL_INT (0, ag_calib_archive_pack (SAMPLE_SESSION, &data, &len));

    /* Bytes at offset 4096 should be AGCZ magic. */
    TEST_ASSERT_EQUAL_MEMORY ("AGCZ", data + 4096, 4);

    /* Read uncompressed size, inflate, verify AGCAL magic. */
    uint32_t uncomp_size = read_u32 (data + 4096 + 4);
    TEST_ASSERT_TRUE (uncomp_size > 0);

    uint8_t *inflated = g_malloc (uncomp_size);
    uLongf  dest_len  = (uLongf) uncomp_size;
    int zrc = uncompress (inflated, &dest_len, data + 4096 + 8,
                           (uLong) (len - 4096 - 8));
    TEST_ASSERT_EQUAL_INT (Z_OK, zrc);

    /* First 5 bytes of decompressed data should be "AGCAL". */
    TEST_ASSERT_EQUAL_MEMORY ("AGCAL", inflated, 5);

    g_free (inflated);
    g_free (data);
}

void test_agcal_entry_count_is_3 (void)
{
    uint8_t *data = NULL;
    size_t   len  = 0;
    TEST_ASSERT_EQUAL_INT (0, ag_calib_archive_pack (SAMPLE_SESSION, &data, &len));

    /* Decompress the AGCZ payload to get the raw AGCAL. */
    uint32_t uncomp_size = read_u32 (data + 4096 + 4);
    uint8_t *agcal = g_malloc (uncomp_size);
    uLongf   dest_len = (uLongf) uncomp_size;
    TEST_ASSERT_EQUAL_INT (Z_OK, uncompress (agcal, &dest_len,
                                              data + 4096 + 8,
                                              (uLong) (len - 4096 - 8)));

    /* AGCAL header: 8-byte magic, then uint32 entry count. */
    uint32_t n_entries = read_u32 (agcal + 8);
    TEST_ASSERT_EQUAL_UINT32 (3u, n_entries);

    g_free (agcal);
    g_free (data);
}

/* ------------------------------------------------------------------ */
/*  Tests: backward_compat                                             */
/* ------------------------------------------------------------------ */

/*
 * Helper: extract the raw AGCAL blob from a packed archive.
 * Caller must g_free *out_agcal.
 */
static int
extract_raw_agcal (uint8_t **out_agcal, size_t *out_len)
{
    uint8_t *data = NULL;
    size_t   len  = 0;
    if (ag_calib_archive_pack (SAMPLE_SESSION, &data, &len) != 0)
        return -1;

    uint32_t uncomp_size = read_u32 (data + 4096 + 4);
    uint8_t *agcal = g_malloc (uncomp_size);
    uLongf   dest_len = (uLongf) uncomp_size;

    if (uncompress (agcal, &dest_len, data + 4096 + 8,
                     (uLong) (len - 4096 - 8)) != Z_OK) {
        g_free (agcal);
        g_free (data);
        return -1;
    }

    g_free (data);
    *out_agcal = agcal;
    *out_len   = (size_t) dest_len;
    return 0;
}

void test_unpack_raw_agcal (void)
{
    uint8_t *agcal = NULL;
    size_t   agcal_len = 0;
    TEST_ASSERT_EQUAL_INT (0, extract_raw_agcal (&agcal, &agcal_len));

    AgRemapTable *left = NULL, *right = NULL;
    AgCalibMeta   meta = {0};
    TEST_ASSERT_EQUAL_INT (0, ag_calib_archive_unpack (agcal, agcal_len,
                                                        &left, &right, &meta));
    TEST_ASSERT_NOT_NULL (left);
    TEST_ASSERT_NOT_NULL (right);
    TEST_ASSERT_EQUAL_UINT32 (EXPECTED_WIDTH, left->width);

    ag_remap_table_free (left);
    ag_remap_table_free (right);
    g_free (agcal);
}

void test_unpack_bare_agcz (void)
{
    uint8_t *data = NULL;
    size_t   len  = 0;
    TEST_ASSERT_EQUAL_INT (0, ag_calib_archive_pack (SAMPLE_SESSION, &data, &len));

    /* The AGCZ payload starts at offset 4096. */
    const uint8_t *agcz     = data + 4096;
    size_t         agcz_len = len - 4096;

    AgRemapTable *left = NULL, *right = NULL;
    AgCalibMeta   meta = {0};
    TEST_ASSERT_EQUAL_INT (0, ag_calib_archive_unpack (agcz, agcz_len,
                                                        &left, &right, &meta));
    TEST_ASSERT_NOT_NULL (left);
    TEST_ASSERT_NOT_NULL (right);
    TEST_ASSERT_EQUAL_UINT32 (EXPECTED_WIDTH, left->width);

    ag_remap_table_free (left);
    ag_remap_table_free (right);
    g_free (data);
}

void test_unpack_full_agst (void)
{
    uint8_t *data = NULL;
    size_t   len  = 0;
    TEST_ASSERT_EQUAL_INT (0, ag_calib_archive_pack (SAMPLE_SESSION, &data, &len));

    AgRemapTable *left = NULL, *right = NULL;
    AgCalibMeta   meta = {0};
    TEST_ASSERT_EQUAL_INT (0, ag_calib_archive_unpack (data, len,
                                                        &left, &right, &meta));
    TEST_ASSERT_NOT_NULL (left);
    TEST_ASSERT_NOT_NULL (right);

    ag_remap_table_free (left);
    ag_remap_table_free (right);
    g_free (data);
}

/* ------------------------------------------------------------------ */
/*  Tests: error_handling                                               */
/* ------------------------------------------------------------------ */

void test_unpack_null_data (void)
{
    AgRemapTable *left = NULL, *right = NULL;
    int rc = ag_calib_archive_unpack (NULL, 0, &left, &right, NULL);
    TEST_ASSERT_NOT_EQUAL (0, rc);
}

void test_unpack_truncated_magic (void)
{
    uint8_t buf[3] = { 'A', 'G', 'C' };
    AgRemapTable *left = NULL, *right = NULL;
    int rc = ag_calib_archive_unpack (buf, 3, &left, &right, NULL);
    TEST_ASSERT_NOT_EQUAL (0, rc);
}

void test_unpack_bad_magic (void)
{
    uint8_t buf[12] = { 'G', 'A', 'R', 'B', 'A', 'G', 'E', 0,
                         0, 0, 0, 0 };
    AgRemapTable *left = NULL, *right = NULL;
    int rc = ag_calib_archive_unpack (buf, 12, &left, &right, NULL);
    TEST_ASSERT_NOT_EQUAL (0, rc);
}

void test_unpack_corrupted_zlib (void)
{
    uint8_t *data = NULL;
    size_t   len  = 0;
    TEST_ASSERT_EQUAL_INT (0, ag_calib_archive_pack (SAMPLE_SESSION, &data, &len));

    /* Corrupt a chunk of the AGCZ zlib payload (offset 4096 + 16 onwards). */
    for (size_t i = 4112; i < 4112 + 64 && i < len; i++)
        data[i] ^= 0xFF;

    AgRemapTable *left = NULL, *right = NULL;
    int rc = ag_calib_archive_unpack (data, len, &left, &right, NULL);
    TEST_ASSERT_NOT_EQUAL (0, rc);

    g_free (data);
}

void test_pack_nonexistent_dir (void)
{
    uint8_t *data = NULL;
    size_t   len  = 0;
    int rc = ag_calib_archive_pack ("/no/such/path", &data, &len);
    TEST_ASSERT_NOT_EQUAL (0, rc);
    TEST_ASSERT_NULL (data);
}

void test_list_valid_archive (void)
{
    uint8_t *data = NULL;
    size_t   len  = 0;
    TEST_ASSERT_EQUAL_INT (0, ag_calib_archive_pack (SAMPLE_SESSION, &data, &len));

    TEST_ASSERT_EQUAL_INT (0, ag_calib_archive_list (data, len));

    g_free (data);
}

void test_list_header_valid (void)
{
    uint8_t *data = NULL;
    size_t   len  = 0;
    TEST_ASSERT_EQUAL_INT (0, ag_calib_archive_pack (SAMPLE_SESSION, &data, &len));

    /* Pass only the first 4096 bytes (the AGST header). */
    TEST_ASSERT_EQUAL_INT (0, ag_calib_archive_list_header (data, 4096));

    g_free (data);
}

void test_list_header_rejects_non_agst (void)
{
    uint8_t buf[16] = {0};
    memcpy (buf, "NOPE", 4);
    TEST_ASSERT_NOT_EQUAL (0, ag_calib_archive_list_header (buf, 16));
}

/* ------------------------------------------------------------------ */
/*  Tests: multislot                                                   */
/* ------------------------------------------------------------------ */

void test_build_single_slot (void)
{
    uint8_t *agst = NULL;
    size_t   agst_len = 0;
    TEST_ASSERT_EQUAL_INT (0, ag_calib_archive_pack (SAMPLE_SESSION, &agst, &agst_len));

    /* Build AGMS with one slot (no existing data). */
    uint8_t *agms = NULL;
    size_t   agms_len = 0;
    TEST_ASSERT_EQUAL_INT (0, ag_multislot_build (NULL, 0, 0, agst, agst_len,
                                                    &agms, &agms_len));
    TEST_ASSERT_NOT_NULL (agms);
    TEST_ASSERT_TRUE (agms_len > AG_MULTISLOT_HEADER_SIZE);

    /* Verify AGMS magic. */
    TEST_ASSERT_EQUAL_MEMORY (AG_MULTISLOT_MAGIC, agms, AG_MULTISLOT_MAGIC_LEN);

    /* Parse index. */
    AgMultiSlotIndex idx;
    TEST_ASSERT_EQUAL_INT (0, ag_multislot_parse_index (agms, agms_len, &idx));
    TEST_ASSERT_EQUAL_INT (AG_MAX_SLOTS, idx.num_slots);
    TEST_ASSERT_EQUAL_INT (1, idx.slots[0].occupied);
    TEST_ASSERT_EQUAL_INT (0, idx.slots[1].occupied);
    TEST_ASSERT_EQUAL_INT (0, idx.slots[2].occupied);
    TEST_ASSERT_EQUAL_INT (EXPECTED_WIDTH,  idx.slots[0].image_w);
    TEST_ASSERT_EQUAL_INT (EXPECTED_HEIGHT, idx.slots[0].image_h);

    g_free (agms);
    g_free (agst);
}

void test_build_three_slots (void)
{
    uint8_t *agst = NULL;
    size_t   agst_len = 0;
    TEST_ASSERT_EQUAL_INT (0, ag_calib_archive_pack (SAMPLE_SESSION, &agst, &agst_len));

    /* Build incrementally: slot 0, then slot 1, then slot 2. */
    uint8_t *file = NULL;
    size_t   file_len = 0;
    TEST_ASSERT_EQUAL_INT (0, ag_multislot_build (NULL, 0, 0, agst, agst_len,
                                                    &file, &file_len));

    uint8_t *file2 = NULL;
    size_t   file2_len = 0;
    TEST_ASSERT_EQUAL_INT (0, ag_multislot_build (file, file_len, 1, agst, agst_len,
                                                    &file2, &file2_len));
    g_free (file);

    uint8_t *file3 = NULL;
    size_t   file3_len = 0;
    TEST_ASSERT_EQUAL_INT (0, ag_multislot_build (file2, file2_len, 2, agst, agst_len,
                                                    &file3, &file3_len));
    g_free (file2);

    /* All three slots should be occupied. */
    AgMultiSlotIndex idx;
    TEST_ASSERT_EQUAL_INT (0, ag_multislot_parse_index (file3, file3_len, &idx));
    TEST_ASSERT_EQUAL_INT (1, idx.slots[0].occupied);
    TEST_ASSERT_EQUAL_INT (1, idx.slots[1].occupied);
    TEST_ASSERT_EQUAL_INT (1, idx.slots[2].occupied);

    /* Total size should be header + 3 * agst_len. */
    TEST_ASSERT_EQUAL_size_t (AG_MULTISLOT_HEADER_SIZE + 3 * agst_len, file3_len);

    g_free (file3);
    g_free (agst);
}

void test_extract_slot (void)
{
    uint8_t *agst = NULL;
    size_t   agst_len = 0;
    TEST_ASSERT_EQUAL_INT (0, ag_calib_archive_pack (SAMPLE_SESSION, &agst, &agst_len));

    /* Build with all 3 slots. */
    uint8_t *f1 = NULL, *f2 = NULL, *f3 = NULL;
    size_t   l1 = 0, l2 = 0, l3 = 0;
    TEST_ASSERT_EQUAL_INT (0, ag_multislot_build (NULL, 0, 0, agst, agst_len, &f1, &l1));
    TEST_ASSERT_EQUAL_INT (0, ag_multislot_build (f1, l1, 1, agst, agst_len, &f2, &l2));
    g_free (f1);
    TEST_ASSERT_EQUAL_INT (0, ag_multislot_build (f2, l2, 2, agst, agst_len, &f3, &l3));
    g_free (f2);

    /* Extract each slot and verify AGST magic. */
    for (int i = 0; i < 3; i++) {
        const uint8_t *slot_data = NULL;
        size_t         slot_len  = 0;
        TEST_ASSERT_EQUAL_INT (0, ag_multislot_extract_slot (f3, l3, i,
                                                               &slot_data, &slot_len));
        TEST_ASSERT_NOT_NULL (slot_data);
        TEST_ASSERT_EQUAL_size_t (agst_len, slot_len);
        TEST_ASSERT_EQUAL_MEMORY (AG_STASH_MAGIC, slot_data, AG_STASH_MAGIC_LEN);
    }

    g_free (f3);
    g_free (agst);
}

void test_delete_slot (void)
{
    uint8_t *agst = NULL;
    size_t   agst_len = 0;
    TEST_ASSERT_EQUAL_INT (0, ag_calib_archive_pack (SAMPLE_SESSION, &agst, &agst_len));

    /* Build with 3 slots, then delete the middle one. */
    uint8_t *f1 = NULL, *f2 = NULL, *f3 = NULL;
    size_t   l1 = 0, l2 = 0, l3 = 0;
    TEST_ASSERT_EQUAL_INT (0, ag_multislot_build (NULL, 0, 0, agst, agst_len, &f1, &l1));
    TEST_ASSERT_EQUAL_INT (0, ag_multislot_build (f1, l1, 1, agst, agst_len, &f2, &l2));
    g_free (f1);
    TEST_ASSERT_EQUAL_INT (0, ag_multislot_build (f2, l2, 2, agst, agst_len, &f3, &l3));
    g_free (f2);

    /* Delete slot 1. */
    uint8_t *f4 = NULL;
    size_t   l4 = 0;
    TEST_ASSERT_EQUAL_INT (0, ag_multislot_build (f3, l3, 1, NULL, 0, &f4, &l4));
    g_free (f3);

    AgMultiSlotIndex idx;
    TEST_ASSERT_EQUAL_INT (0, ag_multislot_parse_index (f4, l4, &idx));
    TEST_ASSERT_EQUAL_INT (1, idx.slots[0].occupied);
    TEST_ASSERT_EQUAL_INT (0, idx.slots[1].occupied);
    TEST_ASSERT_EQUAL_INT (1, idx.slots[2].occupied);

    /* Size should be header + 2 * agst_len. */
    TEST_ASSERT_EQUAL_size_t (AG_MULTISLOT_HEADER_SIZE + 2 * agst_len, l4);

    /* Slot 2's offset should come right after slot 0. */
    TEST_ASSERT_EQUAL_UINT32 (AG_MULTISLOT_HEADER_SIZE, idx.slots[0].offset);
    TEST_ASSERT_EQUAL_UINT32 (AG_MULTISLOT_HEADER_SIZE + agst_len, idx.slots[2].offset);

    g_free (f4);
    g_free (agst);
}

void test_legacy_migration (void)
{
    /* Pack a normal AGST blob (legacy single-slot). */
    uint8_t *agst = NULL;
    size_t   agst_len = 0;
    TEST_ASSERT_EQUAL_INT (0, ag_calib_archive_pack (SAMPLE_SESSION, &agst, &agst_len));

    /* Pass the AGST as "existing" and upload to slot 1.
     * The legacy AGST should be migrated to slot 0. */
    uint8_t *agms = NULL;
    size_t   agms_len = 0;
    TEST_ASSERT_EQUAL_INT (0, ag_multislot_build (agst, agst_len, 1, agst, agst_len,
                                                    &agms, &agms_len));

    AgMultiSlotIndex idx;
    TEST_ASSERT_EQUAL_INT (0, ag_multislot_parse_index (agms, agms_len, &idx));
    TEST_ASSERT_EQUAL_INT (1, idx.slots[0].occupied);   /* migrated legacy */
    TEST_ASSERT_EQUAL_INT (1, idx.slots[1].occupied);   /* new upload */
    TEST_ASSERT_EQUAL_INT (0, idx.slots[2].occupied);

    /* Both slots should have valid AGST data. */
    const uint8_t *s0, *s1;
    size_t s0_len, s1_len;
    TEST_ASSERT_EQUAL_INT (0, ag_multislot_extract_slot (agms, agms_len, 0, &s0, &s0_len));
    TEST_ASSERT_EQUAL_INT (0, ag_multislot_extract_slot (agms, agms_len, 1, &s1, &s1_len));
    TEST_ASSERT_EQUAL_MEMORY (AG_STASH_MAGIC, s0, AG_STASH_MAGIC_LEN);
    TEST_ASSERT_EQUAL_MEMORY (AG_STASH_MAGIC, s1, AG_STASH_MAGIC_LEN);

    g_free (agms);
    g_free (agst);
}

void test_extract_from_legacy (void)
{
    /* A bare AGST blob: slot 0 should work, slot 1 should fail. */
    uint8_t *agst = NULL;
    size_t   agst_len = 0;
    TEST_ASSERT_EQUAL_INT (0, ag_calib_archive_pack (SAMPLE_SESSION, &agst, &agst_len));

    const uint8_t *slot_data = NULL;
    size_t         slot_len  = 0;
    TEST_ASSERT_EQUAL_INT (0, ag_multislot_extract_slot (agst, agst_len, 0,
                                                          &slot_data, &slot_len));
    TEST_ASSERT_EQUAL_size_t (agst_len, slot_len);

    TEST_ASSERT_NOT_EQUAL (0, ag_multislot_extract_slot (agst, agst_len, 1,
                                                          &slot_data, &slot_len));

    g_free (agst);
}

void test_all_empty_returns_zero_len (void)
{
    uint8_t *agst = NULL;
    size_t   agst_len = 0;
    TEST_ASSERT_EQUAL_INT (0, ag_calib_archive_pack (SAMPLE_SESSION, &agst, &agst_len));

    /* Build with one slot, then delete it. */
    uint8_t *f1 = NULL;
    size_t   l1 = 0;
    TEST_ASSERT_EQUAL_INT (0, ag_multislot_build (NULL, 0, 0, agst, agst_len, &f1, &l1));

    uint8_t *f2 = NULL;
    size_t   l2 = 0;
    TEST_ASSERT_EQUAL_INT (0, ag_multislot_build (f1, l1, 0, NULL, 0, &f2, &l2));
    g_free (f1);

    /* All slots empty — out_len should be 0. */
    TEST_ASSERT_EQUAL_size_t (0u, l2);
    TEST_ASSERT_NULL (f2);

    g_free (agst);
}

void test_list_header_multislot (void)
{
    uint8_t *agst = NULL;
    size_t   agst_len = 0;
    TEST_ASSERT_EQUAL_INT (0, ag_calib_archive_pack (SAMPLE_SESSION, &agst, &agst_len));

    /* Build with slots 0 and 2. */
    uint8_t *f1 = NULL, *f2 = NULL;
    size_t   l1 = 0, l2 = 0;
    TEST_ASSERT_EQUAL_INT (0, ag_multislot_build (NULL, 0, 0, agst, agst_len, &f1, &l1));
    TEST_ASSERT_EQUAL_INT (0, ag_multislot_build (f1, l1, 2, agst, agst_len, &f2, &l2));
    g_free (f1);

    /* list_header should succeed (prints to stdout). */
    TEST_ASSERT_EQUAL_INT (0, ag_multislot_list_header (f2, l2));

    g_free (f2);
    g_free (agst);
}

/* ------------------------------------------------------------------ */
/*  Main                                                               */
/* ------------------------------------------------------------------ */

int
main (void)
{
    UNITY_BEGIN ();

    /* pack_unpack_roundtrip */
    RUN_TEST (test_pack_sample_session);
    RUN_TEST (test_roundtrip_remap_dimensions);
    RUN_TEST (test_roundtrip_metadata);
    RUN_TEST (test_roundtrip_remap_data_integrity);

    /* archive_format */
    RUN_TEST (test_output_is_agst_envelope);
    RUN_TEST (test_agst_header_contains_valid_json);
    RUN_TEST (test_agcz_payload_at_offset_4096);
    RUN_TEST (test_agcal_entry_count_is_3);

    /* backward_compat */
    RUN_TEST (test_unpack_raw_agcal);
    RUN_TEST (test_unpack_bare_agcz);
    RUN_TEST (test_unpack_full_agst);

    /* error_handling */
    RUN_TEST (test_unpack_null_data);
    RUN_TEST (test_unpack_truncated_magic);
    RUN_TEST (test_unpack_bad_magic);
    RUN_TEST (test_unpack_corrupted_zlib);
    RUN_TEST (test_pack_nonexistent_dir);
    RUN_TEST (test_list_valid_archive);
    RUN_TEST (test_list_header_valid);
    RUN_TEST (test_list_header_rejects_non_agst);

    /* multislot */
    RUN_TEST (test_build_single_slot);
    RUN_TEST (test_build_three_slots);
    RUN_TEST (test_extract_slot);
    RUN_TEST (test_delete_slot);
    RUN_TEST (test_legacy_migration);
    RUN_TEST (test_extract_from_legacy);
    RUN_TEST (test_all_empty_returns_zero_len);
    RUN_TEST (test_list_header_multislot);

    return UNITY_END ();
}
