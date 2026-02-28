/*
 * remap.h — pre-computed pixel remap for stereo rectification
 */

#ifndef AG_REMAP_H
#define AG_REMAP_H

#include <glib.h>
#include <stdint.h>

/*
 * Binary remap file format (RMAP):
 *
 *   Offset  Size         Description
 *   ──────  ───────────  ──────────────────────────────────
 *   0       4            Magic: "RMAP"
 *   4       4            uint32_le  width
 *   8       4            uint32_le  height
 *   12      4            uint32_le  flags
 *   16      W*H*N        pixel offsets (N = 4 if flags=0, 3 if flags=1)
 *
 * Flags:
 *   0  Standard format: each offset is 4 bytes (uint32_le).
 *   1  Compact format:  each offset is 3 bytes (low 3 bytes of uint32_le).
 *      Used by the calibration archive packer to save ~25% storage.
 *      The compact sentinel (out-of-bounds) is 0xFFFFFF.
 *
 * The standard format (flags=0) is the canonical on-disk representation
 * produced by the calibration notebook and ag_remap_table_save().
 * The compact format (flags=1) is only used inside AGCAL archives and
 * is transparent to callers — load functions expand it automatically.
 */
#define AG_REMAP_MAGIC     "RMAP"
#define AG_REMAP_SENTINEL  0xFFFFFFFFu

typedef struct {
    uint32_t  width;
    uint32_t  height;
    uint32_t *offsets;   /* width * height entries; SENTINEL = out-of-bounds */
} AgRemapTable;

/*
 * Load a .bin remap file exported by the calibration notebook.
 * Returns NULL on error (prints its own diagnostic).
 * Caller must ag_remap_table_free() when done.
 */
AgRemapTable *ag_remap_table_load (const char *path);

/*
 * Load a remap table from an in-memory buffer (same binary format as the
 * .bin file: 4-byte magic "RMAP", uint32 width, uint32 height, uint32 flags,
 * then width*height uint32 offsets).
 * Returns NULL on error (prints its own diagnostic).
 * Caller must ag_remap_table_free() when done.
 */
AgRemapTable *ag_remap_table_load_from_memory (const uint8_t *data, size_t len);

/*
 * Save a remap table to a .bin file in standard 4-byte-per-offset format.
 * Returns 0 on success, -1 on error (prints its own diagnostic).
 */
int           ag_remap_table_save (const AgRemapTable *table, const char *path);

void          ag_remap_table_free (AgRemapTable *table);

/*
 * Apply nearest-neighbour remap on RGB24 data.
 * src and dst must each be width*height*3 bytes.  dst must not alias src.
 * Uses NEON on aarch64, scalar fallback otherwise.
 */
void ag_remap_rgb (const AgRemapTable *table,
                   const guint8 *src, guint8 *dst);

/*
 * Apply nearest-neighbour remap on single-channel (grayscale) data.
 * src and dst must each be width*height bytes.  dst must not alias src.
 * Uses NEON on aarch64, scalar fallback otherwise.
 * The same offset table is used — offsets are pixel indices, not byte offsets.
 */
void ag_remap_gray (const AgRemapTable *table,
                    const guint8 *src, guint8 *dst);

#endif /* AG_REMAP_H */
