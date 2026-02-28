/*
 * calib_load.c — unified calibration loader
 *
 * Consolidates the calibration loading logic previously duplicated in
 * cmd_stream.c and cmd_depth_preview.c.
 */

#include "calib_load.h"
#include "calib_archive.h"
#include "device_file.h"
#include "../vendor/cJSON.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/*  Metadata-only loader                                               */
/* ------------------------------------------------------------------ */

int
ag_calib_load_meta (const char *session_path, AgCalibMeta *out)
{
    char *json_path = g_build_filename (session_path, "calib_result",
                                         "calibration_meta.json", NULL);
    gchar *contents = NULL;
    gsize  length   = 0;
    GError *error   = NULL;

    if (!g_file_get_contents (json_path, &contents, &length, &error)) {
        fprintf (stderr, "warn: cannot read %s: %s\n",
                 json_path, error ? error->message : "unknown error");
        g_clear_error (&error);
        g_free (json_path);
        return -1;
    }
    g_free (json_path);

    cJSON *root = cJSON_ParseWithLength (contents, length);
    g_free (contents);

    if (!root) {
        fprintf (stderr, "warn: failed to parse calibration_meta.json\n");
        return -1;
    }

    /* disparity_range object. */
    cJSON *dr = cJSON_GetObjectItemCaseSensitive (root, "disparity_range");
    if (dr) {
        cJSON *md = cJSON_GetObjectItemCaseSensitive (dr, "min_disparity");
        cJSON *nd = cJSON_GetObjectItemCaseSensitive (dr, "num_disparities");
        if (cJSON_IsNumber (md)) out->min_disparity   = md->valueint;
        if (cJSON_IsNumber (nd)) out->num_disparities  = nd->valueint;
    }

    cJSON *fl = cJSON_GetObjectItemCaseSensitive (root, "focal_length_px");
    if (cJSON_IsNumber (fl)) out->focal_length_px = fl->valuedouble;

    cJSON *bl = cJSON_GetObjectItemCaseSensitive (root, "baseline_cm");
    if (cJSON_IsNumber (bl)) out->baseline_cm = bl->valuedouble;

    cJSON_Delete (root);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Load from local filesystem path                                    */
/* ------------------------------------------------------------------ */

static int
load_from_local (const char *session_path,
                 AgRemapTable **out_left, AgRemapTable **out_right,
                 AgCalibMeta *out_meta)
{
    char *lpath = g_build_filename (session_path, "calib_result",
                                     "remap_left.bin", NULL);
    char *rpath = g_build_filename (session_path, "calib_result",
                                     "remap_right.bin", NULL);

    *out_left  = ag_remap_table_load (lpath);
    *out_right = ag_remap_table_load (rpath);
    g_free (lpath);
    g_free (rpath);

    if (!*out_left || !*out_right) {
        fprintf (stderr, "error: failed to load remap tables from %s\n",
                 session_path);
        ag_remap_table_free (*out_left);
        ag_remap_table_free (*out_right);
        *out_left  = NULL;
        *out_right = NULL;
        return -1;
    }

    if (out_meta)
        ag_calib_load_meta (session_path, out_meta);

    return 0;
}

/* ------------------------------------------------------------------ */
/*  Load from on-camera slot                                           */
/* ------------------------------------------------------------------ */

static int
load_from_slot (ArvDevice *device, int slot,
                AgRemapTable **out_left, AgRemapTable **out_right,
                AgCalibMeta *out_meta)
{
    uint8_t *archive_data = NULL;
    size_t   archive_len  = 0;

    printf ("Reading calibration from camera (slot %d)...\n", slot);
    if (ag_device_file_read (device, "UserFile1",
                              &archive_data, &archive_len) != 0) {
        fprintf (stderr, "error: failed to read calibration from camera\n");
        return -1;
    }

    /* Extract the requested slot (handles AGMS and legacy AGST). */
    const uint8_t *slot_data = NULL;
    size_t         slot_len  = 0;
    if (ag_multislot_extract_slot (archive_data, archive_len,
                                    slot,
                                    &slot_data, &slot_len) != 0) {
        fprintf (stderr, "error: calibration slot %d not found\n", slot);
        g_free (archive_data);
        return -1;
    }

    AgCalibMeta meta_tmp = {0};
    if (ag_calib_archive_unpack (slot_data, slot_len,
                                  out_left, out_right,
                                  &meta_tmp) != 0) {
        fprintf (stderr, "error: failed to unpack calibration archive\n");
        g_free (archive_data);
        return -1;
    }

    g_free (archive_data);

    if (out_meta)
        *out_meta = meta_tmp;

    return 0;
}

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

int
ag_calib_load (ArvDevice *device,
               const AgCalibSource *source,
               AgRemapTable **out_left,
               AgRemapTable **out_right,
               AgCalibMeta *out_meta)
{
    *out_left  = NULL;
    *out_right = NULL;

    if (source->local_path)
        return load_from_local (source->local_path, out_left, out_right,
                                out_meta);

    if (source->slot >= 0)
        return load_from_slot (device, source->slot, out_left, out_right,
                               out_meta);

    fprintf (stderr, "error: no calibration source specified\n");
    return -1;
}
