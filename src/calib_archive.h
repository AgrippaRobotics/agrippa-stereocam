/*
 * calib_archive.h — calibration data archive for on-camera storage
 *
 * Packs remap_left.bin, remap_right.bin, and calibration_meta.json from
 * a calibration session folder into a single flat archive suitable for
 * storage in the camera's ~11 MB UserFile.
 *
 * Inner archive format (AGCAL, little-endian):
 *
 *   Offset  Size   Description
 *   ──────  ─────  ────────────────────────────────
 *   0       8      Magic: "AGCAL\x00\x00\x01"
 *   8       4      uint32  n_entries
 *
 *   Per entry:
 *     4      uint32  name_len   (incl. null terminator)
 *     4      uint32  data_len
 *     name_len       null-terminated file name
 *     data_len       raw file bytes
 *
 * On-camera file layout (AGST stash envelope):
 *
 *   Offset      Size   Description
 *   ──────────  ─────  ────────────────────────────────
 *   0           4      Magic: "AGST"
 *   4           4      uint32  header_size (AG_STASH_HEADER_SIZE)
 *   8           N      JSON metadata summary (null-terminated, zero-padded)
 *   header_size ...    AGCZ compressed archive (see below)
 *
 * Compressed archive (AGCZ):
 *
 *   Offset  Size   Description
 *   ──────  ─────  ────────────────────────────────
 *   0       4      Magic: "AGCZ"
 *   4       4      uint32  uncompressed size
 *   8       ...    zlib-compressed AGCAL archive
 *
 * Multi-slot container (AGMS):
 *
 *   Offset      Size   Description
 *   ──────────  ─────  ────────────────────────────────
 *   0           4      Magic: "AGMS"
 *   4           4      uint32  header_size (AG_MULTISLOT_HEADER_SIZE)
 *   8           4      uint32  num_slots   (AG_MAX_SLOTS)
 *   12          N      JSON slot index (null-terminated, zero-padded)
 *   header_size ...    Concatenated AGST blobs
 *
 * The "list" command reads only the first header_size bytes from the
 * camera to display calibration metadata, avoiding a full download.
 * The pack function produces an AGST blob (header + AGCZ payload).
 * The unpack function accepts AGST, AGCZ, or raw AGCAL.
 */

#ifndef AG_CALIB_ARCHIVE_H
#define AG_CALIB_ARCHIVE_H

#include "common.h"
#include "remap.h"

#include <stdint.h>
#include <stddef.h>

#define AG_CALIB_ARCHIVE_MAGIC      "AGCAL\x00\x00\x01"
#define AG_CALIB_ARCHIVE_MAGIC_LEN  8

/* Stash header: fixed 4 KB block at the front of the on-camera file. */
#define AG_STASH_MAGIC       "AGST"
#define AG_STASH_MAGIC_LEN   4
#define AG_STASH_HEADER_SIZE 4096

/*
 * Pack the calibration session's calib_result/ directory into a single
 * on-camera blob: a fixed-size AGST header (JSON metadata summary)
 * followed by an AGCZ compressed archive.
 *
 * On success, *out_data is a newly-allocated buffer (caller must g_free)
 * and *out_len is its size.  Returns 0 on success, -1 on error.
 */
int ag_calib_archive_pack (const char *session_path,
                           uint8_t **out_data, size_t *out_len);

/*
 * Unpack an on-camera blob and reconstruct the remap tables and metadata.
 * Accepts AGST (header + AGCZ), bare AGCZ, or raw AGCAL.
 *
 * On success, *out_left and *out_right are newly-allocated AgRemapTable
 * structs (caller must ag_remap_table_free) and *out_meta is populated.
 * out_meta may be NULL if metadata is not needed.
 * Returns 0 on success, -1 on error.
 */
int ag_calib_archive_unpack (const uint8_t *data, size_t len,
                             AgRemapTable **out_left,
                             AgRemapTable **out_right,
                             AgCalibMeta *out_meta);

/*
 * Print the table-of-contents and calibration summary of an archive.
 * Accepts AGST, AGCZ, or raw AGCAL.
 * Returns 0 on success, -1 on error.
 */
int ag_calib_archive_list (const uint8_t *data, size_t len);

/*
 * Print calibration summary from just the AGST header (first 4 KB).
 * Does NOT require downloading the full archive.
 * Returns 0 on success, -1 if the data is not an AGST header.
 */
int ag_calib_archive_list_header (const uint8_t *data, size_t len);

/*
 * Extract an on-camera blob to a session directory on disk.
 * Accepts AGST (header + AGCZ), bare AGCZ, or raw AGCAL.
 *
 * Creates output_dir/calib_result/ and writes:
 *   remap_left.bin        (standard 4-byte-per-offset RMAP format)
 *   remap_right.bin       (standard 4-byte-per-offset RMAP format)
 *   calibration_meta.json (verbatim from archive)
 *
 * Returns 0 on success, -1 on error.
 */
int ag_calib_archive_extract_to_dir (const uint8_t *data, size_t len,
                                      const char *output_dir);

/* ------------------------------------------------------------------ */
/*  Multi-slot container (AGMS)                                        */
/* ------------------------------------------------------------------ */

#define AG_MULTISLOT_MAGIC        "AGMS"
#define AG_MULTISLOT_MAGIC_LEN    4
#define AG_MULTISLOT_HEADER_SIZE  4096
#define AG_MAX_SLOTS              3

typedef struct {
    int       occupied;         /* 0 = empty, 1 = has data */
    uint32_t  offset;           /* byte offset from start of AGMS file */
    uint32_t  size;             /* AGST blob size in bytes */
    int       image_w, image_h;
    double    rms_stereo_px;
    char      packed_at[32];
} AgSlotInfo;

typedef struct {
    int        num_slots;
    AgSlotInfo slots[AG_MAX_SLOTS];
} AgMultiSlotIndex;

/*
 * Parse the AGMS index from the file header (first 4096 bytes).
 * Returns 0 on success, -1 if the data is not a valid AGMS header.
 */
int ag_multislot_parse_index (const uint8_t *data, size_t len,
                               AgMultiSlotIndex *out);

/*
 * Print a summary table for all slots from just the AGMS header.
 * Returns 0 on success, -1 on error.
 */
int ag_multislot_list_header (const uint8_t *data, size_t len);

/*
 * Build (or rebuild) a complete AGMS file with one slot updated.
 *
 * existing_data may be NULL (empty camera), an AGST blob (legacy
 * single-slot — migrated to slot 0), or an AGMS file.
 *
 * slot: 0 .. AG_MAX_SLOTS-1.
 * archive / archive_len: the new AGST blob for that slot, or
 *   NULL / 0 to delete the slot.
 *
 * On success, *out_data is a newly-allocated AGMS file (caller
 * must g_free) and *out_len is its size.  If all slots are empty
 * after the operation, *out_len is set to 0 (caller should delete
 * the file on the camera).
 *
 * Returns 0 on success, -1 on error.
 */
int ag_multislot_build (const uint8_t *existing_data, size_t existing_len,
                         int slot,
                         const uint8_t *archive, size_t archive_len,
                         uint8_t **out_data, size_t *out_len);

/*
 * Extract a single slot's AGST blob from an AGMS (or legacy AGST) file.
 *
 * Returns a pointer into the input data buffer (NOT a copy — do not
 * free the returned pointer independently).
 *
 * For legacy AGST input, slot 0 returns the entire blob;
 * other slots return -1 (not present).
 *
 * Returns 0 on success, -1 if the slot is empty or invalid.
 */
int ag_multislot_extract_slot (const uint8_t *data, size_t len,
                                int slot,
                                const uint8_t **out_slot_data,
                                size_t *out_slot_len);

#endif /* AG_CALIB_ARCHIVE_H */
