/*
 * remap.h — pre-computed pixel remap for stereo rectification
 */

#ifndef AG_REMAP_H
#define AG_REMAP_H

#include <glib.h>
#include <stdint.h>

/* Binary remap file magic and sentinel. */
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
