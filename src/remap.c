/*
 * remap.c — pre-computed pixel remap for stereo rectification
 *
 * Loads binary offset tables exported by the calibration notebook and
 * applies nearest-neighbour remapping on RGB24 frames.  On aarch64 the
 * inner loop uses NEON for bulk offset loads and output stores.
 */

#include "remap.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __aarch64__
#include <arm_neon.h>
#endif

/* ------------------------------------------------------------------ */
/*  Binary file loader                                                 */
/* ------------------------------------------------------------------ */

AgRemapTable *
ag_remap_table_load (const char *path)
{
    FILE *f = fopen (path, "rb");
    if (!f) {
        fprintf (stderr, "remap: cannot open %s: %s\n", path, strerror (errno));
        return NULL;
    }

    /* Read 16-byte header: magic(4) + width(4) + height(4) + flags(4). */
    char     magic[4];
    uint32_t header[3];   /* width, height, flags */

    if (fread (magic, 1, 4, f) != 4 ||
        fread (header, sizeof (uint32_t), 3, f) != 3) {
        fprintf (stderr, "remap: %s: truncated header\n", path);
        fclose (f);
        return NULL;
    }

    if (memcmp (magic, AG_REMAP_MAGIC, 4) != 0) {
        fprintf (stderr, "remap: %s: bad magic (expected RMAP)\n", path);
        fclose (f);
        return NULL;
    }

    uint32_t width  = header[0];
    uint32_t height = header[1];
    /* header[2] = flags, reserved — ignored for now. */

    if (width == 0 || height == 0 || width > 8192 || height > 8192) {
        fprintf (stderr, "remap: %s: implausible dimensions %ux%u\n",
                 path, width, height);
        fclose (f);
        return NULL;
    }

    size_t n_pixels = (size_t) width * height;
    uint32_t *offsets = g_malloc (n_pixels * sizeof (uint32_t));

    if (fread (offsets, sizeof (uint32_t), n_pixels, f) != n_pixels) {
        fprintf (stderr, "remap: %s: truncated data (expected %zu offsets)\n",
                 path, n_pixels);
        g_free (offsets);
        fclose (f);
        return NULL;
    }

    fclose (f);

    AgRemapTable *table = g_malloc (sizeof (AgRemapTable));
    table->width   = width;
    table->height  = height;
    table->offsets = offsets;

    return table;
}

void
ag_remap_table_free (AgRemapTable *table)
{
    if (!table)
        return;
    g_free (table->offsets);
    g_free (table);
}

/* ------------------------------------------------------------------ */
/*  Scalar remap (portable fallback)                                   */
/* ------------------------------------------------------------------ */

static void
remap_rgb_scalar (const uint32_t *offsets, uint32_t n_pixels,
                  const guint8 *src, guint8 *dst)
{
    for (uint32_t i = 0; i < n_pixels; i++) {
        uint32_t off = offsets[i];
        guint8 *d = dst + (size_t) i * 3;
        if (off != AG_REMAP_SENTINEL) {
            const guint8 *s = src + (size_t) off * 3;
            d[0] = s[0];
            d[1] = s[1];
            d[2] = s[2];
        } else {
            d[0] = 0;
            d[1] = 0;
            d[2] = 0;
        }
    }
}

/* ------------------------------------------------------------------ */
/*  NEON remap (aarch64)                                               */
/* ------------------------------------------------------------------ */

#ifdef __aarch64__

static void
remap_rgb_neon (const uint32_t *offsets, uint32_t n_pixels,
                const guint8 *src, guint8 *dst)
{
    uint32_t i = 0;

    /* Process 8 pixels (24 output bytes) per iteration.
     * The gather is scalar (ARM NEON has no gather instruction), but
     * bulk offset loading and output stores are vectorised. */
    for (; i + 8 <= n_pixels; i += 8) {
        uint32x4_t off_lo = vld1q_u32 (offsets + i);
        uint32x4_t off_hi = vld1q_u32 (offsets + i + 4);

        uint32_t o[8];
        vst1q_u32 (o,     off_lo);
        vst1q_u32 (o + 4, off_hi);

        guint8 tmp[24];
        for (int k = 0; k < 8; k++) {
            if (o[k] != AG_REMAP_SENTINEL) {
                const guint8 *s = src + (size_t) o[k] * 3;
                tmp[k * 3]     = s[0];
                tmp[k * 3 + 1] = s[1];
                tmp[k * 3 + 2] = s[2];
            } else {
                tmp[k * 3]     = 0;
                tmp[k * 3 + 1] = 0;
                tmp[k * 3 + 2] = 0;
            }
        }

        /* Store 24 bytes: 16 via vst1q_u8, 8 via vst1_u8. */
        guint8 *d = dst + (size_t) i * 3;
        vst1q_u8 (d,      vld1q_u8 (tmp));
        vst1_u8  (d + 16, vld1_u8  (tmp + 16));
    }

    /* Scalar tail for remaining pixels. */
    if (i < n_pixels)
        remap_rgb_scalar (offsets + i, n_pixels - i, src, dst + (size_t) i * 3);
}

#endif /* __aarch64__ */

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

void
ag_remap_rgb (const AgRemapTable *table, const guint8 *src, guint8 *dst)
{
    uint32_t n = table->width * table->height;
#ifdef __aarch64__
    remap_rgb_neon (table->offsets, n, src, dst);
#else
    remap_rgb_scalar (table->offsets, n, src, dst);
#endif
}

/* ------------------------------------------------------------------ */
/*  Grayscale (single-channel) remap                                   */
/* ------------------------------------------------------------------ */

static void
remap_gray_scalar (const uint32_t *offsets, uint32_t n_pixels,
                   const guint8 *src, guint8 *dst)
{
    for (uint32_t i = 0; i < n_pixels; i++) {
        uint32_t off = offsets[i];
        dst[i] = (off != AG_REMAP_SENTINEL) ? src[off] : 0;
    }
}

#ifdef __aarch64__

static void
remap_gray_neon (const uint32_t *offsets, uint32_t n_pixels,
                 const guint8 *src, guint8 *dst)
{
    uint32_t i = 0;

    /* Process 8 pixels per iteration. */
    for (; i + 8 <= n_pixels; i += 8) {
        uint32x4_t off_lo = vld1q_u32 (offsets + i);
        uint32x4_t off_hi = vld1q_u32 (offsets + i + 4);

        uint32_t o[8];
        vst1q_u32 (o,     off_lo);
        vst1q_u32 (o + 4, off_hi);

        uint8_t tmp[8];
        for (int k = 0; k < 8; k++)
            tmp[k] = (o[k] != AG_REMAP_SENTINEL) ? src[o[k]] : 0;

        vst1_u8 (dst + i, vld1_u8 (tmp));
    }

    /* Scalar tail. */
    if (i < n_pixels)
        remap_gray_scalar (offsets + i, n_pixels - i, src, dst + i);
}

#endif /* __aarch64__ */

void
ag_remap_gray (const AgRemapTable *table, const guint8 *src, guint8 *dst)
{
    uint32_t n = table->width * table->height;
#ifdef __aarch64__
    remap_gray_neon (table->offsets, n, src, dst);
#else
    remap_gray_scalar (table->offsets, n, src, dst);
#endif
}
