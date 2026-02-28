/*
 * calib_archive.c — calibration data archive for on-camera storage
 */

#include "calib_archive.h"
#include "../vendor/cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <zlib.h>

/*
 * Compressed envelope: when zlib is used, the packed AGCAL archive is
 * deflated and wrapped in a thin header so the reader can detect and
 * decompress it transparently.
 *
 *   Offset  Size   Description
 *   ──────  ─────  ────────────────────────────
 *   0       4      Magic: "AGCZ"
 *   4       4      uint32_le  uncompressed size
 *   8       ...    zlib-compressed AGCAL archive
 */
#define AG_CALIB_COMPRESSED_MAGIC      "AGCZ"
#define AG_CALIB_COMPRESSED_MAGIC_LEN  4

/*
 * Compress an AGCAL archive blob with zlib deflate.
 * Returns a new g_malloc'd buffer with the AGCZ envelope, or NULL on
 * error.  Caller must g_free().
 */
static uint8_t *
compress_archive (const uint8_t *data, size_t len, size_t *out_len)
{
    *out_len = 0;

    uLongf bound = compressBound ((uLong) len);
    uint8_t *buf = g_malloc (AG_CALIB_COMPRESSED_MAGIC_LEN + 4 + bound);

    /* Header. */
    memcpy (buf, AG_CALIB_COMPRESSED_MAGIC, AG_CALIB_COMPRESSED_MAGIC_LEN);
    uint32_t orig = (uint32_t) len;
    buf[4] = (uint8_t) (orig);
    buf[5] = (uint8_t) (orig >> 8);
    buf[6] = (uint8_t) (orig >> 16);
    buf[7] = (uint8_t) (orig >> 24);

    /* Compress. */
    uLongf compressed_len = bound;
    int zrc = compress2 (buf + 8, &compressed_len, data, (uLong) len,
                         Z_BEST_COMPRESSION);
    if (zrc != Z_OK) {
        fprintf (stderr, "calib_archive: zlib compress2 failed (%d)\n", zrc);
        g_free (buf);
        return NULL;
    }

    *out_len = (size_t) (8 + compressed_len);
    return buf;
}

/*
 * If data starts with AGST, skip past the fixed header to the payload.
 * Returns adjusted pointer and length, or the originals if no header.
 */
static const uint8_t *
skip_stash_header (const uint8_t *data, size_t len, size_t *out_len)
{
    if (len >= 8 && memcmp (data, AG_STASH_MAGIC, AG_STASH_MAGIC_LEN) == 0) {
        uint32_t hdr_size = (uint32_t) data[4]
                          | ((uint32_t) data[5] << 8)
                          | ((uint32_t) data[6] << 16)
                          | ((uint32_t) data[7] << 24);
        if (hdr_size <= len) {
            *out_len = len - hdr_size;
            return data + hdr_size;
        }
    }
    *out_len = len;
    return data;
}

/*
 * If data starts with AGCZ, decompress and return the inner AGCAL
 * archive.  If it already starts with AGCAL, return NULL (caller
 * should use the data as-is).  On decompression error, prints a
 * diagnostic and returns NULL with *was_compressed = 1.
 *
 * NOTE: callers must strip any AGST header first via skip_stash_header().
 */
static uint8_t *
try_decompress (const uint8_t *data, size_t len, size_t *out_len,
                int *was_compressed)
{
    *out_len = 0;
    *was_compressed = 0;

    if (len < 8)
        return NULL;

    if (memcmp (data, AG_CALIB_COMPRESSED_MAGIC,
                AG_CALIB_COMPRESSED_MAGIC_LEN) != 0)
        return NULL;   /* not compressed — caller uses original data */

    *was_compressed = 1;

    uint32_t orig_len = (uint32_t) data[4]
                      | ((uint32_t) data[5] << 8)
                      | ((uint32_t) data[6] << 16)
                      | ((uint32_t) data[7] << 24);

    uint8_t *out = g_malloc (orig_len);
    uLongf  dest_len = (uLongf) orig_len;

    int zrc = uncompress (out, &dest_len, data + 8, (uLong) (len - 8));
    if (zrc != Z_OK) {
        fprintf (stderr, "calib_archive: zlib uncompress failed (%d)\n", zrc);
        g_free (out);
        return NULL;
    }

    *out_len = (size_t) dest_len;
    return out;
}

/* Files packed into the archive (in order). */
static const char *k_archive_files[] = {
    "remap_left.bin",
    "remap_right.bin",
    "calibration_meta.json",
};
#define N_ARCHIVE_FILES  (sizeof k_archive_files / sizeof k_archive_files[0])

/* ------------------------------------------------------------------ */
/*  Pack                                                               */
/* ------------------------------------------------------------------ */

/* Read an entire file into a g_malloc'd buffer.  Returns -1 on error. */
static int
read_file (const char *path, uint8_t **out_data, size_t *out_len)
{
    gchar  *contents = NULL;
    gsize   length   = 0;
    GError *err      = NULL;

    if (!g_file_get_contents (path, &contents, &length, &err)) {
        fprintf (stderr, "calib_archive: cannot read %s: %s\n",
                 path, err ? err->message : "unknown error");
        g_clear_error (&err);
        return -1;
    }

    *out_data = (uint8_t *) contents;
    *out_len  = (size_t) length;
    return 0;
}

/* Append a uint32 in little-endian to a GByteArray. */
static void
append_u32 (GByteArray *buf, uint32_t v)
{
    uint8_t bytes[4];
    bytes[0] = (uint8_t) (v);
    bytes[1] = (uint8_t) (v >> 8);
    bytes[2] = (uint8_t) (v >> 16);
    bytes[3] = (uint8_t) (v >> 24);
    g_byte_array_append (buf, bytes, 4);
}

/* Read a uint32 LE from a byte pointer. */
static uint32_t
read_u32 (const uint8_t *p)
{
    return (uint32_t) p[0]
         | ((uint32_t) p[1] << 8)
         | ((uint32_t) p[2] << 16)
         | ((uint32_t) p[3] << 24);
}

/*
 * Compact a remap .bin from 4 bytes/offset to 3 bytes/offset.
 *
 * The offsets are pixel indices into a 1440×1080 (or 720×540) image, so the
 * maximum value is well under 2^24 = 16,777,216.  By dropping the unused
 * high byte we save 25% — enough to squeeze binning=1 data into the
 * camera's ~11 MB available storage.
 *
 * The RMAP header's flags field (offset 12) is set to 1 to mark the
 * compact format.  The sentinel 0xFFFFFFFF maps to 0xFFFFFF.
 *
 * Returns a new g_malloc'd buffer; caller must g_free().
 */
#define AG_REMAP_COMPACT_FLAG  1
#define AG_REMAP_COMPACT_SENTINEL  0x00FFFFFFu

static uint8_t *
pack_remap_compact (const uint8_t *data, size_t len, size_t *out_len)
{
    *out_len = 0;

    if (len < 16 || memcmp (data, AG_REMAP_MAGIC, 4) != 0)
        return NULL;

    uint32_t width  = read_u32 (data + 4);
    uint32_t height = read_u32 (data + 8);
    size_t n_pixels = (size_t) width * height;

    if (len < 16 + n_pixels * 4)
        return NULL;

    size_t compact_len = 16 + n_pixels * 3;
    uint8_t *out = g_malloc (compact_len);

    /* Copy the 16-byte header, then set flags = COMPACT. */
    memcpy (out, data, 16);
    uint32_t flags = AG_REMAP_COMPACT_FLAG;
    memcpy (out + 12, &flags, 4);

    /* Pack each 4-byte offset → 3 bytes (little-endian low 3 bytes). */
    const uint8_t *src = data + 16;
    uint8_t       *dst = out  + 16;

    for (size_t i = 0; i < n_pixels; i++) {
        uint32_t off = read_u32 (src + i * 4);
        if (off == AG_REMAP_SENTINEL)
            off = AG_REMAP_COMPACT_SENTINEL;
        dst[i * 3]     = (uint8_t) (off);
        dst[i * 3 + 1] = (uint8_t) (off >> 8);
        dst[i * 3 + 2] = (uint8_t) (off >> 16);
    }

    *out_len = compact_len;
    return out;
}

/*
 * Expand a compact remap buffer back to the standard 4-byte-per-offset
 * format so it can be passed to ag_remap_table_load_from_memory().
 * Returns a new g_malloc'd buffer; caller must g_free().
 */
static uint8_t *
unpack_remap_compact (const uint8_t *data, size_t len, size_t *out_len)
{
    *out_len = 0;

    if (len < 16)
        return NULL;

    uint32_t width  = read_u32 (data + 4);
    uint32_t height = read_u32 (data + 8);
    uint32_t flags  = read_u32 (data + 12);

    if (flags != AG_REMAP_COMPACT_FLAG)
        return NULL;   /* not compact format */

    size_t n_pixels = (size_t) width * height;

    if (len < 16 + n_pixels * 3)
        return NULL;

    size_t full_len = 16 + n_pixels * 4;
    uint8_t *out = g_malloc (full_len);

    /* Copy header, clear the compact flag. */
    memcpy (out, data, 16);
    flags = 0;
    memcpy (out + 12, &flags, 4);

    /* Expand 3-byte offsets → 4-byte. */
    const uint8_t *src = data + 16;
    uint8_t       *dst = out  + 16;

    for (size_t i = 0; i < n_pixels; i++) {
        uint32_t off = (uint32_t) src[i * 3]
                     | ((uint32_t) src[i * 3 + 1] << 8)
                     | ((uint32_t) src[i * 3 + 2] << 16);
        if (off == AG_REMAP_COMPACT_SENTINEL)
            off = AG_REMAP_SENTINEL;
        uint8_t b[4] = { (uint8_t) off, (uint8_t) (off >> 8),
                         (uint8_t) (off >> 16), (uint8_t) (off >> 24) };
        memcpy (dst + i * 4, b, 4);
    }

    *out_len = full_len;
    return out;
}

/* Return true if file name looks like a remap .bin entry. */
static int
is_remap_entry (const char *name)
{
    return strcmp (name, "remap_left.bin")  == 0
        || strcmp (name, "remap_right.bin") == 0;
}

int
ag_calib_archive_pack (const char *session_path,
                       uint8_t **out_data, size_t *out_len)
{
    *out_data = NULL;
    *out_len  = 0;

    /* Read all component files. */
    uint8_t *file_data[N_ARCHIVE_FILES] = {NULL};
    size_t   file_len[N_ARCHIVE_FILES]  = {0};

    for (size_t i = 0; i < N_ARCHIVE_FILES; i++) {
        char *path = g_build_filename (session_path, "calib_result",
                                        k_archive_files[i], NULL);
        int rc = read_file (path, &file_data[i], &file_len[i]);
        g_free (path);

        if (rc != 0) {
            /* calibration_meta.json is optional. */
            if (i == 2) {
                file_data[i] = NULL;
                file_len[i]  = 0;
                continue;
            }
            /* remap files are mandatory. */
            for (size_t j = 0; j < i; j++)
                g_free (file_data[j]);
            return -1;
        }
    }

    /*
     * Inject a "packed_at" ISO 8601 timestamp into the metadata JSON
     * so the archive is self-documenting.  Also build a compact summary
     * JSON for the AGST header (readable without downloading the full
     * archive).
     */
    char *header_json = NULL;

    if (file_data[2] && file_len[2] > 0) {
        cJSON *root = cJSON_ParseWithLength ((const char *) file_data[2],
                                              file_len[2]);
        if (root) {
            time_t now = time (NULL);
            struct tm *utc = gmtime (&now);
            char ts[32];
            strftime (ts, sizeof ts, "%Y-%m-%dT%H:%M:%SZ", utc);
            cJSON_DeleteItemFromObjectCaseSensitive (root, "packed_at");
            cJSON_AddStringToObject (root, "packed_at", ts);

            /* Build the compact header summary (subset of fields). */
            cJSON *hdr = cJSON_CreateObject ();
            cJSON *isz = cJSON_GetObjectItemCaseSensitive (root, "image_size");
            if (isz) cJSON_AddItemReferenceToObject (hdr, "image_size", isz);
            cJSON *np = cJSON_GetObjectItemCaseSensitive (root, "num_pairs_used");
            if (np)  cJSON_AddNumberToObject (hdr, "num_pairs_used", np->valuedouble);
            cJSON *rms = cJSON_GetObjectItemCaseSensitive (root, "rms_stereo_px");
            if (rms) cJSON_AddNumberToObject (hdr, "rms_stereo_px", rms->valuedouble);
            cJSON *epi = cJSON_GetObjectItemCaseSensitive (root, "mean_epipolar_error_px");
            if (epi) cJSON_AddNumberToObject (hdr, "mean_epipolar_error_px", epi->valuedouble);
            cJSON *bl = cJSON_GetObjectItemCaseSensitive (root, "baseline_cm");
            if (bl)  cJSON_AddNumberToObject (hdr, "baseline_cm", bl->valuedouble);
            cJSON *fl = cJSON_GetObjectItemCaseSensitive (root, "focal_length_px");
            if (fl)  cJSON_AddNumberToObject (hdr, "focal_length_px", fl->valuedouble);
            cJSON *dr = cJSON_GetObjectItemCaseSensitive (root, "disparity_range");
            if (dr)  cJSON_AddItemReferenceToObject (hdr, "disparity_range", dr);
            cJSON *pa = cJSON_GetObjectItemCaseSensitive (root, "packed_at");
            if (pa)  cJSON_AddStringToObject (hdr, "packed_at", pa->valuestring);
            header_json = cJSON_Print (hdr);
            cJSON_Delete (hdr);

            /* Re-serialize the full JSON (with packed_at) for the archive. */
            char *json_str = cJSON_PrintUnformatted (root);
            cJSON_Delete (root);

            if (json_str) {
                g_free (file_data[2]);
                /* cJSON uses stdlib malloc; copy to g_malloc'd buffer. */
                size_t slen = strlen (json_str);
                file_data[2] = g_malloc (slen);
                memcpy (file_data[2], json_str, slen);
                file_len[2]  = slen;
                free (json_str);
            }
        }
    }

    /*
     * Compact remap tables: 4 bytes/offset → 3 bytes/offset.
     * This saves ~25% and is required to fit binning=1 data into the
     * camera's ~11 MB available UserFile storage.
     */
    for (size_t i = 0; i < N_ARCHIVE_FILES; i++) {
        if (!file_data[i] || !is_remap_entry (k_archive_files[i]))
            continue;

        size_t compact_len = 0;
        uint8_t *compact = pack_remap_compact (file_data[i], file_len[i],
                                               &compact_len);
        if (compact) {
            printf ("  %-18s  %7.1f KB → %7.1f KB (compact 3-byte offsets)\n",
                    k_archive_files[i],
                    (double) file_len[i]  / 1024.0,
                    (double) compact_len / 1024.0);
            g_free (file_data[i]);
            file_data[i] = compact;
            file_len[i]  = compact_len;
        }
    }

    /* Count entries with non-zero data. */
    uint32_t n_entries = 0;
    for (size_t i = 0; i < N_ARCHIVE_FILES; i++) {
        if (file_data[i])
            n_entries++;
    }

    /* Build the archive. */
    GByteArray *buf = g_byte_array_new ();

    /* Magic. */
    g_byte_array_append (buf, (const uint8_t *) AG_CALIB_ARCHIVE_MAGIC,
                         AG_CALIB_ARCHIVE_MAGIC_LEN);

    /* Entry count. */
    append_u32 (buf, n_entries);

    /* Entries. */
    for (size_t i = 0; i < N_ARCHIVE_FILES; i++) {
        if (!file_data[i])
            continue;

        uint32_t name_len = (uint32_t) strlen (k_archive_files[i]) + 1;
        uint32_t data_len = (uint32_t) file_len[i];

        append_u32 (buf, name_len);
        append_u32 (buf, data_len);
        g_byte_array_append (buf, (const uint8_t *) k_archive_files[i], name_len);
        g_byte_array_append (buf, file_data[i], data_len);
    }

    /* Clean up component files. */
    for (size_t i = 0; i < N_ARCHIVE_FILES; i++)
        g_free (file_data[i]);

    /* Compress the whole archive with zlib. */
    size_t   raw_len  = buf->len;
    uint8_t *raw_data = g_byte_array_free (buf, FALSE);

    size_t   compressed_len = 0;
    uint8_t *compressed = compress_archive (raw_data, raw_len, &compressed_len);

    if (compressed) {
        printf ("  zlib:  %.1f MB → %.1f MB (%.0f%% reduction)\n",
                (double) raw_len / (1024.0 * 1024.0),
                (double) compressed_len / (1024.0 * 1024.0),
                (1.0 - (double) compressed_len / (double) raw_len) * 100.0);
        g_free (raw_data);
        raw_data = compressed;
        raw_len  = compressed_len;
    } else {
        fprintf (stderr,
                 "calib_archive: warn: compression failed, using raw archive\n");
        /* raw_data/raw_len already set */
    }

    /*
     * Build the AGST stash envelope: a fixed-size header (4 KB) containing
     * the metadata JSON summary, followed by the AGCZ (or raw) archive.
     * This lets the 'list' command read just the header from the camera.
     */
    size_t total_len = AG_STASH_HEADER_SIZE + raw_len;
    uint8_t *stash = g_malloc0 (total_len);   /* zero-fills header padding */

    /* AGST magic + header size. */
    memcpy (stash, AG_STASH_MAGIC, AG_STASH_MAGIC_LEN);
    uint32_t hdr_size = AG_STASH_HEADER_SIZE;
    stash[4] = (uint8_t) (hdr_size);
    stash[5] = (uint8_t) (hdr_size >> 8);
    stash[6] = (uint8_t) (hdr_size >> 16);
    stash[7] = (uint8_t) (hdr_size >> 24);

    /* Write the JSON summary into the header (null-terminated, padded). */
    if (header_json) {
        size_t json_len = strlen (header_json);
        size_t max_json = AG_STASH_HEADER_SIZE - 8;   /* room after magic+size */
        if (json_len > max_json - 1)
            json_len = max_json - 1;   /* leave room for null terminator */
        memcpy (stash + 8, header_json, json_len);
        /* stash is zero-filled, so null terminator is implicit. */
        free (header_json);
        header_json = NULL;
    }

    /* Append the archive payload. */
    memcpy (stash + AG_STASH_HEADER_SIZE, raw_data, raw_len);
    g_free (raw_data);

    printf ("  header: %d bytes (metadata summary)\n", AG_STASH_HEADER_SIZE);

    *out_data = stash;
    *out_len  = total_len;
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Iteration helper                                                   */
/* ------------------------------------------------------------------ */

typedef int (*entry_visitor_fn) (const char *name, uint32_t name_len,
                                 const uint8_t *data, uint32_t data_len,
                                 void *user_data);

/*
 * Walk every entry in the archive, calling visitor for each.
 * Returns 0 if all entries visited, or the first non-zero return from
 * the visitor, or -1 on format error.
 */
static int
archive_foreach (const uint8_t *data, size_t len, entry_visitor_fn visitor,
                 void *user_data)
{
    if (len < AG_CALIB_ARCHIVE_MAGIC_LEN + 4) {
        fprintf (stderr, "calib_archive: buffer too small\n");
        return -1;
    }

    if (memcmp (data, AG_CALIB_ARCHIVE_MAGIC, AG_CALIB_ARCHIVE_MAGIC_LEN) != 0) {
        fprintf (stderr, "calib_archive: bad magic\n");
        return -1;
    }

    uint32_t n_entries = read_u32 (data + AG_CALIB_ARCHIVE_MAGIC_LEN);
    size_t offset = AG_CALIB_ARCHIVE_MAGIC_LEN + 4;

    for (uint32_t i = 0; i < n_entries; i++) {
        if (offset + 8 > len) {
            fprintf (stderr, "calib_archive: truncated entry header at #%u\n", i);
            return -1;
        }

        uint32_t name_len = read_u32 (data + offset);
        uint32_t data_len = read_u32 (data + offset + 4);
        offset += 8;

        if (offset + name_len + data_len > len) {
            fprintf (stderr, "calib_archive: truncated entry data at #%u\n", i);
            return -1;
        }

        const char    *name = (const char *) (data + offset);
        const uint8_t *edata = data + offset + name_len;

        int rc = visitor (name, name_len, edata, data_len, user_data);
        if (rc != 0)
            return rc;

        offset += name_len + data_len;
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/*  Unpack                                                             */
/* ------------------------------------------------------------------ */

typedef struct {
    AgRemapTable *left;
    AgRemapTable *right;
    AgCalibMeta  *meta;
} UnpackCtx;

/*
 * Try to load a remap table from archive entry data.  If the entry uses
 * compact 3-byte offsets (flags == 1), expand to standard 4-byte format
 * first.  Returns NULL on error.
 */
static AgRemapTable *
load_remap_entry (const uint8_t *data, uint32_t data_len)
{
    if (data_len >= 16) {
        uint32_t flags = read_u32 (data + 12);
        if (flags == AG_REMAP_COMPACT_FLAG) {
            size_t   expanded_len = 0;
            uint8_t *expanded = unpack_remap_compact (data, data_len,
                                                      &expanded_len);
            if (!expanded) {
                fprintf (stderr,
                         "calib_archive: failed to expand compact remap\n");
                return NULL;
            }
            AgRemapTable *t = ag_remap_table_load_from_memory (
                                  expanded, expanded_len);
            g_free (expanded);
            return t;
        }
    }

    return ag_remap_table_load_from_memory (data, data_len);
}

static int
unpack_visitor (const char *name, uint32_t name_len,
                const uint8_t *data, uint32_t data_len,
                void *user_data)
{
    (void) name_len;
    UnpackCtx *ctx = user_data;

    if (strcmp (name, "remap_left.bin") == 0) {
        ctx->left = load_remap_entry (data, data_len);
        if (!ctx->left)
            return -1;
    } else if (strcmp (name, "remap_right.bin") == 0) {
        ctx->right = load_remap_entry (data, data_len);
        if (!ctx->right)
            return -1;
    } else if (strcmp (name, "calibration_meta.json") == 0 && ctx->meta) {
        /* Parse JSON metadata. */
        cJSON *root = cJSON_ParseWithLength ((const char *) data, data_len);
        if (root) {
            cJSON *dr = cJSON_GetObjectItemCaseSensitive (root, "disparity_range");
            if (dr) {
                cJSON *md = cJSON_GetObjectItemCaseSensitive (dr, "min_disparity");
                cJSON *nd = cJSON_GetObjectItemCaseSensitive (dr, "num_disparities");
                if (cJSON_IsNumber (md)) ctx->meta->min_disparity   = md->valueint;
                if (cJSON_IsNumber (nd)) ctx->meta->num_disparities = nd->valueint;
            }
            cJSON *fl = cJSON_GetObjectItemCaseSensitive (root, "focal_length_px");
            if (cJSON_IsNumber (fl)) ctx->meta->focal_length_px = fl->valuedouble;
            cJSON *bl = cJSON_GetObjectItemCaseSensitive (root, "baseline_cm");
            if (cJSON_IsNumber (bl)) ctx->meta->baseline_cm = bl->valuedouble;
            cJSON_Delete (root);
        } else {
            fprintf (stderr, "calib_archive: warn: failed to parse calibration_meta.json\n");
        }
    }

    return 0;
}

int
ag_calib_archive_unpack (const uint8_t *data, size_t len,
                         AgRemapTable **out_left,
                         AgRemapTable **out_right,
                         AgCalibMeta *out_meta)
{
    *out_left  = NULL;
    *out_right = NULL;
    if (out_meta)
        memset (out_meta, 0, sizeof *out_meta);

    /* Strip AGST header if present, then decompress AGCZ if present. */
    size_t payload_len;
    const uint8_t *payload = skip_stash_header (data, len, &payload_len);

    size_t   inner_len = 0;
    int      was_compressed = 0;
    uint8_t *inner = try_decompress (payload, payload_len,
                                      &inner_len, &was_compressed);

    if (was_compressed && !inner)
        return -1;   /* decompression failed */

    const uint8_t *archive_data = inner ? inner : payload;
    size_t         archive_len  = inner ? inner_len : payload_len;

    UnpackCtx ctx = { .left = NULL, .right = NULL, .meta = out_meta };

    int rc = archive_foreach (archive_data, archive_len, unpack_visitor, &ctx);
    if (rc != 0) {
        ag_remap_table_free (ctx.left);
        ag_remap_table_free (ctx.right);
        g_free (inner);
        return -1;
    }

    if (!ctx.left || !ctx.right) {
        fprintf (stderr, "calib_archive: archive missing remap table(s)\n");
        ag_remap_table_free (ctx.left);
        ag_remap_table_free (ctx.right);
        g_free (inner);
        return -1;
    }

    *out_left  = ctx.left;
    *out_right = ctx.right;
    g_free (inner);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  List                                                               */
/* ------------------------------------------------------------------ */

static int
list_visitor (const char *name, uint32_t name_len,
              const uint8_t *data, uint32_t data_len,
              void *user_data)
{
    (void) name_len;
    (void) data;

    int *index = user_data;
    double size_kb = (double) data_len / 1024.0;

    if (size_kb >= 1024.0)
        printf ("  [%d]  %-28s  %8.1f MB\n", *index, name,
                size_kb / 1024.0);
    else
        printf ("  [%d]  %-28s  %8.1f KB\n", *index, name, size_kb);

    (*index)++;
    return 0;
}

/* Print a calibration summary from a parsed cJSON root. */
static void
print_calib_summary (cJSON *root)
{
    printf ("\nCalibration summary:\n");

    cJSON *isz = cJSON_GetObjectItemCaseSensitive (root, "image_size");
    if (cJSON_IsArray (isz) && cJSON_GetArraySize (isz) >= 2) {
        int w = cJSON_GetArrayItem (isz, 0)->valueint;
        int h = cJSON_GetArrayItem (isz, 1)->valueint;
        printf ("  Resolution:       %d × %d\n", w, h);
    }

    cJSON *np = cJSON_GetObjectItemCaseSensitive (root, "num_pairs_used");
    if (cJSON_IsNumber (np))
        printf ("  Pairs used:       %d\n", np->valueint);

    cJSON *rms = cJSON_GetObjectItemCaseSensitive (root, "rms_stereo_px");
    if (cJSON_IsNumber (rms))
        printf ("  Stereo RMS:       %.4f px\n", rms->valuedouble);

    cJSON *epi = cJSON_GetObjectItemCaseSensitive (root,
                                                    "mean_epipolar_error_px");
    if (cJSON_IsNumber (epi))
        printf ("  Epipolar error:   %.4f px (mean)\n", epi->valuedouble);

    cJSON *bl = cJSON_GetObjectItemCaseSensitive (root, "baseline_cm");
    if (cJSON_IsNumber (bl))
        printf ("  Baseline:         %.2f cm\n", bl->valuedouble);

    cJSON *fl = cJSON_GetObjectItemCaseSensitive (root, "focal_length_px");
    if (cJSON_IsNumber (fl))
        printf ("  Focal length:     %.2f px\n", fl->valuedouble);

    cJSON *dr = cJSON_GetObjectItemCaseSensitive (root, "disparity_range");
    if (dr) {
        cJSON *md = cJSON_GetObjectItemCaseSensitive (dr, "min_disparity");
        cJSON *nd = cJSON_GetObjectItemCaseSensitive (dr, "num_disparities");
        if (cJSON_IsNumber (md) && cJSON_IsNumber (nd))
            printf ("  Disparity range:  %d .. %d (%d values)\n",
                    md->valueint, md->valueint + nd->valueint, nd->valueint);
    }

    cJSON *pa = cJSON_GetObjectItemCaseSensitive (root, "packed_at");
    if (cJSON_IsString (pa) && pa->valuestring)
        printf ("  Packed at:        %s\n", pa->valuestring);
}

/*
 * Visitor that extracts and prints a human-readable summary from the
 * calibration_meta.json entry.
 */
static int
meta_visitor (const char *name, uint32_t name_len,
              const uint8_t *data, uint32_t data_len,
              void *user_data)
{
    (void) name_len;
    (void) user_data;

    if (strcmp (name, "calibration_meta.json") != 0)
        return 0;

    cJSON *root = cJSON_ParseWithLength ((const char *) data, data_len);
    if (!root)
        return 0;

    print_calib_summary (root);
    cJSON_Delete (root);
    return 0;
}

int
ag_calib_archive_list (const uint8_t *data, size_t len)
{
    /* Strip AGST header if present, then decompress AGCZ if present. */
    size_t payload_len;
    const uint8_t *payload = skip_stash_header (data, len, &payload_len);

    size_t   inner_len = 0;
    int      was_compressed = 0;
    uint8_t *inner = try_decompress (payload, payload_len,
                                      &inner_len, &was_compressed);

    if (was_compressed && !inner)
        return -1;

    const uint8_t *archive_data = inner ? inner : payload;
    size_t         archive_len  = inner ? inner_len : payload_len;

    if (archive_len < AG_CALIB_ARCHIVE_MAGIC_LEN + 4) {
        fprintf (stderr, "calib_archive: buffer too small\n");
        g_free (inner);
        return -1;
    }

    if (memcmp (archive_data, AG_CALIB_ARCHIVE_MAGIC,
                AG_CALIB_ARCHIVE_MAGIC_LEN) != 0) {
        fprintf (stderr, "calib_archive: bad magic\n");
        g_free (inner);
        return -1;
    }

    uint32_t n_entries = read_u32 (archive_data + AG_CALIB_ARCHIVE_MAGIC_LEN);

    if (was_compressed) {
        printf ("Calibration archive: %u file(s), %zu bytes on-camera "
                "(%zu bytes uncompressed)\n",
                n_entries, len, archive_len);
    } else {
        printf ("Calibration archive: %u file(s), %zu bytes total\n",
                n_entries, archive_len);
    }

    int index = 0;
    int rc = archive_foreach (archive_data, archive_len, list_visitor, &index);

    /* Second pass: extract and display calibration metadata summary. */
    if (rc == 0)
        archive_foreach (archive_data, archive_len, meta_visitor, NULL);

    g_free (inner);
    return rc;
}

int
ag_calib_archive_list_header (const uint8_t *data, size_t len)
{
    if (len < 8 || memcmp (data, AG_STASH_MAGIC, AG_STASH_MAGIC_LEN) != 0)
        return -1;   /* not an AGST header */

    /* The JSON starts at offset 8, null-terminated within the header. */
    const char *json_str = (const char *) (data + 8);
    size_t max_json = len - 8;

    /* Ensure null-termination within the buffer. */
    size_t json_len = strnlen (json_str, max_json);
    if (json_len == 0)
        return -1;

    cJSON *root = cJSON_ParseWithLength (json_str, json_len);
    if (!root) {
        fprintf (stderr, "calib_archive: failed to parse header metadata\n");
        return -1;
    }

    print_calib_summary (root);
    cJSON_Delete (root);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Extract to directory                                               */
/* ------------------------------------------------------------------ */

typedef struct {
    const char *output_dir;
    int         n_written;
} ExtractCtx;

/*
 * Visitor that writes each archive entry to disk.
 *
 * GOTCHA: remap .bin files are stored in compact 3-byte-per-offset format
 * inside the archive (see pack_remap_compact).  We must re-expand them to
 * the standard 4-byte format on extraction so that:
 *   (a) downstream tools (calibration notebook, ag_remap_table_load) can
 *       read them without special handling, and
 *   (b) downloaded files are byte-identical to the originals that were
 *       uploaded (round-trip integrity).
 *
 * Non-remap entries (e.g. calibration_meta.json) are written verbatim.
 * Note: the JSON will contain a "packed_at" timestamp added during pack,
 * so it won't be byte-identical to the original input JSON.
 */
static int
extract_visitor (const char *name, uint32_t name_len,
                 const uint8_t *data, uint32_t data_len,
                 void *user_data)
{
    (void) name_len;
    ExtractCtx *ctx = user_data;

    char *path = g_build_filename (ctx->output_dir, "calib_result", name, NULL);

    if (is_remap_entry (name)) {
        /* Load via the standard remap loader (handles compact expansion). */
        AgRemapTable *table = load_remap_entry (data, data_len);
        if (!table) {
            fprintf (stderr, "calib_archive: failed to load %s from archive\n",
                     name);
            g_free (path);
            return -1;
        }

        if (ag_remap_table_save (table, path) != 0) {
            ag_remap_table_free (table);
            g_free (path);
            return -1;
        }

        ag_remap_table_free (table);
    } else {
        /* Write raw bytes (JSON, etc.). */
        GError *err = NULL;
        if (!g_file_set_contents (path, (const gchar *) data,
                                   (gssize) data_len, &err)) {
            fprintf (stderr, "calib_archive: failed to write %s: %s\n",
                     path, err ? err->message : "unknown error");
            g_clear_error (&err);
            g_free (path);
            return -1;
        }
    }

    printf ("  %s (%u bytes)\n", name, data_len);
    ctx->n_written++;
    g_free (path);
    return 0;
}

int
ag_calib_archive_extract_to_dir (const uint8_t *data, size_t len,
                                  const char *output_dir)
{
    /* Create output_dir/calib_result/ */
    char *result_dir = g_build_filename (output_dir, "calib_result", NULL);
    if (g_mkdir_with_parents (result_dir, 0755) != 0) {
        fprintf (stderr, "calib_archive: failed to create %s\n", result_dir);
        g_free (result_dir);
        return -1;
    }
    g_free (result_dir);

    /* Strip AGST header, decompress AGCZ. */
    size_t payload_len;
    const uint8_t *payload = skip_stash_header (data, len, &payload_len);

    size_t   inner_len = 0;
    int      was_compressed = 0;
    uint8_t *inner = try_decompress (payload, payload_len,
                                      &inner_len, &was_compressed);

    if (was_compressed && !inner)
        return -1;

    const uint8_t *archive_data = inner ? inner : payload;
    size_t         archive_len  = inner ? inner_len : payload_len;

    ExtractCtx ctx = { .output_dir = output_dir, .n_written = 0 };
    int rc = archive_foreach (archive_data, archive_len,
                               extract_visitor, &ctx);

    g_free (inner);

    if (rc != 0)
        return -1;

    if (ctx.n_written == 0) {
        fprintf (stderr, "calib_archive: archive contained no entries\n");
        return -1;
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/*  Multi-slot container (AGMS)                                        */
/* ------------------------------------------------------------------ */

/*
 * Extract the JSON summary embedded in an AGST header blob.
 * Returns a cJSON* that the caller must cJSON_Delete(), or NULL.
 */
static cJSON *
agst_header_json (const uint8_t *agst, size_t agst_len)
{
    if (agst_len < 8
        || memcmp (agst, AG_STASH_MAGIC, AG_STASH_MAGIC_LEN) != 0)
        return NULL;

    /* AGST layout: magic(4) + header_size(4) + JSON. */
    const char *json_str = (const char *) (agst + 8);
    size_t max_json = (agst_len < AG_STASH_HEADER_SIZE
                       ? agst_len : AG_STASH_HEADER_SIZE) - 8;
    size_t json_len = strnlen (json_str, max_json);
    if (json_len == 0)
        return NULL;

    return cJSON_ParseWithLength (json_str, json_len);
}

int
ag_multislot_parse_index (const uint8_t *data, size_t len,
                           AgMultiSlotIndex *out)
{
    memset (out, 0, sizeof *out);

    if (!data || len < 12)
        return -1;
    if (memcmp (data, AG_MULTISLOT_MAGIC, AG_MULTISLOT_MAGIC_LEN) != 0)
        return -1;

    /* Read header_size (offset 4) and num_slots (offset 8). */
    uint32_t header_size = (uint32_t) data[4]
                         | ((uint32_t) data[5] << 8)
                         | ((uint32_t) data[6] << 16)
                         | ((uint32_t) data[7] << 24);
    uint32_t num_slots   = (uint32_t) data[8]
                         | ((uint32_t) data[9] << 8)
                         | ((uint32_t) data[10] << 16)
                         | ((uint32_t) data[11] << 24);

    if (header_size > len || num_slots > AG_MAX_SLOTS)
        return -1;

    out->num_slots = (int) num_slots;

    /* Parse JSON index starting at offset 12. */
    const char *json_str = (const char *) (data + 12);
    size_t max_json = header_size - 12;
    size_t json_len = strnlen (json_str, max_json);
    if (json_len == 0)
        return 0;   /* no JSON — all slots empty */

    cJSON *root = cJSON_ParseWithLength (json_str, json_len);
    if (!root)
        return -1;

    cJSON *slots = cJSON_GetObjectItemCaseSensitive (root, "slots");
    if (!cJSON_IsArray (slots)) {
        cJSON_Delete (root);
        return -1;
    }

    int arr_size = cJSON_GetArraySize (slots);
    for (int i = 0; i < arr_size && i < AG_MAX_SLOTS; i++) {
        cJSON *entry = cJSON_GetArrayItem (slots, i);
        if (cJSON_IsNull (entry) || !cJSON_IsObject (entry))
            continue;

        AgSlotInfo *si = &out->slots[i];
        si->occupied = 1;

        cJSON *v;
        v = cJSON_GetObjectItemCaseSensitive (entry, "offset");
        if (cJSON_IsNumber (v)) si->offset = (uint32_t) v->valuedouble;
        v = cJSON_GetObjectItemCaseSensitive (entry, "size");
        if (cJSON_IsNumber (v)) si->size   = (uint32_t) v->valuedouble;

        cJSON *isz = cJSON_GetObjectItemCaseSensitive (entry, "image_size");
        if (cJSON_IsArray (isz) && cJSON_GetArraySize (isz) >= 2) {
            si->image_w = cJSON_GetArrayItem (isz, 0)->valueint;
            si->image_h = cJSON_GetArrayItem (isz, 1)->valueint;
        }

        v = cJSON_GetObjectItemCaseSensitive (entry, "rms_stereo_px");
        if (cJSON_IsNumber (v)) si->rms_stereo_px = v->valuedouble;

        v = cJSON_GetObjectItemCaseSensitive (entry, "packed_at");
        if (cJSON_IsString (v) && v->valuestring) {
            strncpy (si->packed_at, v->valuestring, sizeof si->packed_at - 1);
            si->packed_at[sizeof si->packed_at - 1] = '\0';
        }
    }

    cJSON_Delete (root);
    return 0;
}

int
ag_multislot_list_header (const uint8_t *data, size_t len)
{
    AgMultiSlotIndex idx;
    if (ag_multislot_parse_index (data, len, &idx) != 0)
        return -1;

    printf ("\nCalibration slots (%d total):\n", idx.num_slots);

    for (int i = 0; i < idx.num_slots; i++) {
        AgSlotInfo *si = &idx.slots[i];
        if (!si->occupied) {
            printf ("  Slot %d: (empty)\n", i);
            continue;
        }

        printf ("  Slot %d: %dx%d", i, si->image_w, si->image_h);
        if (si->rms_stereo_px > 0.0)
            printf ("  RMS %.4f px", si->rms_stereo_px);
        if (si->packed_at[0])
            printf ("  packed %s", si->packed_at);
        printf ("  (%.1f MB)\n", (double) si->size / (1024.0 * 1024.0));
    }

    return 0;
}

/*
 * Build the JSON index for the AGMS header from the slot info array.
 * The index extracts metadata from each occupied slot's AGST header.
 */
static char *
build_agms_json_index (const AgSlotInfo *slots, int num_slots)
{
    cJSON *root = cJSON_CreateObject ();
    cJSON *arr  = cJSON_CreateArray ();

    for (int i = 0; i < num_slots; i++) {
        if (!slots[i].occupied) {
            cJSON_AddItemToArray (arr, cJSON_CreateNull ());
            continue;
        }

        cJSON *entry = cJSON_CreateObject ();
        cJSON_AddNumberToObject (entry, "offset", (double) slots[i].offset);
        cJSON_AddNumberToObject (entry, "size",   (double) slots[i].size);

        if (slots[i].image_w > 0 && slots[i].image_h > 0) {
            int dims[2] = { slots[i].image_w, slots[i].image_h };
            cJSON *isz = cJSON_CreateIntArray (dims, 2);
            cJSON_AddItemToObject (entry, "image_size", isz);
        }

        if (slots[i].rms_stereo_px > 0.0)
            cJSON_AddNumberToObject (entry, "rms_stereo_px",
                                     slots[i].rms_stereo_px);

        if (slots[i].packed_at[0])
            cJSON_AddStringToObject (entry, "packed_at", slots[i].packed_at);

        cJSON_AddItemToArray (arr, entry);
    }

    cJSON_AddItemToObject (root, "slots", arr);
    char *json = cJSON_Print (root);
    cJSON_Delete (root);
    return json;
}

/*
 * Fill an AgSlotInfo from the JSON stored in an AGST header.
 * The offset and size fields are NOT set here (caller's responsibility).
 */
static void
slot_info_from_agst (const uint8_t *agst, size_t agst_len, AgSlotInfo *si)
{
    si->occupied = 1;
    si->image_w = si->image_h = 0;
    si->rms_stereo_px = 0.0;
    si->packed_at[0] = '\0';

    cJSON *root = agst_header_json (agst, agst_len);
    if (!root)
        return;

    cJSON *isz = cJSON_GetObjectItemCaseSensitive (root, "image_size");
    if (cJSON_IsArray (isz) && cJSON_GetArraySize (isz) >= 2) {
        si->image_w = cJSON_GetArrayItem (isz, 0)->valueint;
        si->image_h = cJSON_GetArrayItem (isz, 1)->valueint;
    }

    cJSON *rms = cJSON_GetObjectItemCaseSensitive (root, "rms_stereo_px");
    if (cJSON_IsNumber (rms))
        si->rms_stereo_px = rms->valuedouble;

    cJSON *pa = cJSON_GetObjectItemCaseSensitive (root, "packed_at");
    if (cJSON_IsString (pa) && pa->valuestring) {
        strncpy (si->packed_at, pa->valuestring, sizeof si->packed_at - 1);
        si->packed_at[sizeof si->packed_at - 1] = '\0';
    }

    cJSON_Delete (root);
}

int
ag_multislot_build (const uint8_t *existing_data, size_t existing_len,
                     int slot,
                     const uint8_t *archive, size_t archive_len,
                     uint8_t **out_data, size_t *out_len)
{
    *out_data = NULL;
    *out_len  = 0;

    if (slot < 0 || slot >= AG_MAX_SLOTS) {
        fprintf (stderr, "calib_archive: slot %d out of range (0..%d)\n",
                 slot, AG_MAX_SLOTS - 1);
        return -1;
    }

    /* Collect existing slot data pointers and sizes. */
    const uint8_t *slot_ptrs[AG_MAX_SLOTS] = { NULL };
    size_t         slot_sizes[AG_MAX_SLOTS] = { 0 };
    AgSlotInfo     slot_info[AG_MAX_SLOTS];
    memset (slot_info, 0, sizeof slot_info);

    if (existing_data && existing_len > 4) {
        if (memcmp (existing_data, AG_MULTISLOT_MAGIC,
                    AG_MULTISLOT_MAGIC_LEN) == 0) {
            /* Existing AGMS file — parse index and collect slot pointers. */
            AgMultiSlotIndex idx;
            if (ag_multislot_parse_index (existing_data, existing_len,
                                           &idx) != 0) {
                fprintf (stderr, "calib_archive: failed to parse existing "
                         "AGMS index\n");
                return -1;
            }

            for (int i = 0; i < idx.num_slots && i < AG_MAX_SLOTS; i++) {
                if (!idx.slots[i].occupied)
                    continue;
                uint32_t off = idx.slots[i].offset;
                uint32_t sz  = idx.slots[i].size;
                if ((size_t) off + sz > existing_len) {
                    fprintf (stderr,
                             "calib_archive: slot %d overflows file "
                             "(offset=%u size=%u file=%zu)\n",
                             i, off, sz, existing_len);
                    return -1;
                }
                slot_ptrs[i]  = existing_data + off;
                slot_sizes[i] = sz;
                slot_info[i]  = idx.slots[i];
            }

        } else if (memcmp (existing_data, AG_STASH_MAGIC,
                           AG_STASH_MAGIC_LEN) == 0) {
            /* Legacy AGST file — migrate to slot 0. */
            slot_ptrs[0]  = existing_data;
            slot_sizes[0] = existing_len;
            slot_info_from_agst (existing_data, existing_len, &slot_info[0]);
            slot_info[0].offset = AG_MULTISLOT_HEADER_SIZE;
            slot_info[0].size   = (uint32_t) existing_len;
        }
        /* else: unknown format — treat as empty. */
    }

    /* Apply the update: replace or delete the target slot. */
    if (archive && archive_len > 0) {
        slot_ptrs[slot]  = archive;
        slot_sizes[slot] = archive_len;
        slot_info_from_agst (archive, archive_len, &slot_info[slot]);
        slot_info[slot].size = (uint32_t) archive_len;
    } else {
        /* Delete this slot. */
        slot_ptrs[slot]  = NULL;
        slot_sizes[slot] = 0;
        memset (&slot_info[slot], 0, sizeof slot_info[slot]);
    }

    /* Check if all slots are empty. */
    int any_occupied = 0;
    for (int i = 0; i < AG_MAX_SLOTS; i++) {
        if (slot_ptrs[i])
            any_occupied = 1;
    }

    if (!any_occupied) {
        *out_len = 0;   /* signal: delete the file */
        return 0;
    }

    /* Calculate offsets (slots packed contiguously after header). */
    uint32_t write_offset = AG_MULTISLOT_HEADER_SIZE;
    for (int i = 0; i < AG_MAX_SLOTS; i++) {
        if (!slot_ptrs[i])
            continue;
        slot_info[i].offset = write_offset;
        slot_info[i].size   = (uint32_t) slot_sizes[i];
        write_offset += (uint32_t) slot_sizes[i];
    }

    size_t total_len = write_offset;

    /* Build JSON index. */
    char *json = build_agms_json_index (slot_info, AG_MAX_SLOTS);
    if (!json) {
        fprintf (stderr, "calib_archive: failed to build AGMS JSON index\n");
        return -1;
    }

    size_t json_len = strlen (json);
    size_t max_json = AG_MULTISLOT_HEADER_SIZE - 12;
    if (json_len > max_json - 1) {
        fprintf (stderr,
                 "calib_archive: AGMS JSON index too large (%zu > %zu)\n",
                 json_len, max_json - 1);
        free (json);
        return -1;
    }

    /* Assemble the output buffer. */
    uint8_t *buf = g_malloc0 (total_len);

    /* AGMS header: magic + header_size + num_slots + JSON. */
    memcpy (buf, AG_MULTISLOT_MAGIC, AG_MULTISLOT_MAGIC_LEN);
    uint32_t hdr_size = AG_MULTISLOT_HEADER_SIZE;
    buf[4]  = (uint8_t) (hdr_size);
    buf[5]  = (uint8_t) (hdr_size >> 8);
    buf[6]  = (uint8_t) (hdr_size >> 16);
    buf[7]  = (uint8_t) (hdr_size >> 24);
    uint32_t ns = AG_MAX_SLOTS;
    buf[8]  = (uint8_t) (ns);
    buf[9]  = (uint8_t) (ns >> 8);
    buf[10] = (uint8_t) (ns >> 16);
    buf[11] = (uint8_t) (ns >> 24);
    memcpy (buf + 12, json, json_len);
    free (json);

    /* Copy slot payloads. */
    for (int i = 0; i < AG_MAX_SLOTS; i++) {
        if (!slot_ptrs[i])
            continue;
        memcpy (buf + slot_info[i].offset, slot_ptrs[i], slot_sizes[i]);
    }

    *out_data = buf;
    *out_len  = total_len;
    return 0;
}

int
ag_multislot_extract_slot (const uint8_t *data, size_t len,
                            int slot,
                            const uint8_t **out_slot_data,
                            size_t *out_slot_len)
{
    *out_slot_data = NULL;
    *out_slot_len  = 0;

    if (!data || len < 4 || slot < 0 || slot >= AG_MAX_SLOTS)
        return -1;

    /* Legacy AGST: only slot 0 is valid. */
    if (memcmp (data, AG_STASH_MAGIC, AG_STASH_MAGIC_LEN) == 0) {
        if (slot != 0)
            return -1;
        *out_slot_data = data;
        *out_slot_len  = len;
        return 0;
    }

    /* AGMS container. */
    if (memcmp (data, AG_MULTISLOT_MAGIC, AG_MULTISLOT_MAGIC_LEN) != 0)
        return -1;

    AgMultiSlotIndex idx;
    if (ag_multislot_parse_index (data, len, &idx) != 0)
        return -1;

    if (slot >= idx.num_slots || !idx.slots[slot].occupied)
        return -1;

    uint32_t off = idx.slots[slot].offset;
    uint32_t sz  = idx.slots[slot].size;

    if ((size_t) off + sz > len) {
        fprintf (stderr, "calib_archive: slot %d data overflows file\n", slot);
        return -1;
    }

    *out_slot_data = data + off;
    *out_slot_len  = sz;
    return 0;
}
