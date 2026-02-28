/*
 * device_file.c — GenICam SFNC FileAccessControl via Aravis
 *
 * Implements read/write/delete/info for the camera's persistent UserFile
 * storage using the GenICam Standard Features Naming Convention (SFNC)
 * FileAccessControl nodes, accessed through Aravis's device and GenICam
 * node-map APIs.
 *
 * Protocol overview (per SFNC 2.x):
 *   1. Select file     → FileSelector = "UserFile1"
 *   2. Open            → FileOpenMode = Read|Write,
 *                         FileOperationSelector = Open,
 *                         execute FileOperationExecute
 *   3. Read/Write loop → set FileAccessOffset/Length,
 *                         FileOperationSelector = Read|Write,
 *                         execute FileOperationExecute,
 *                         access FileAccessBuffer register
 *   4. Close           → FileOperationSelector = Close,
 *                         execute FileOperationExecute
 */

#include "device_file.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ------------------------------------------------------------------ */
/*  Helpers                                                            */
/* ------------------------------------------------------------------ */

/* Set a string/enumeration feature; return 0 on success. */
static int
set_str (ArvDevice *dev, const char *name, const char *value)
{
    GError *err = NULL;
    arv_device_set_string_feature_value (dev, name, value, &err);
    if (err) {
        fprintf (stderr, "device_file: failed to set %s=%s: %s\n",
                 name, value, err->message);
        g_clear_error (&err);
        return -1;
    }
    return 0;
}

/* Execute a command node; return 0 on success. */
static int
exec_cmd (ArvDevice *dev, const char *name)
{
    GError *err = NULL;
    arv_device_execute_command (dev, name, &err);
    if (err) {
        fprintf (stderr, "device_file: command %s failed: %s\n",
                 name, err->message);
        g_clear_error (&err);
        return -1;
    }
    return 0;
}

/* Read an integer feature; return 0 on success. */
static int
get_int (ArvDevice *dev, const char *name, int64_t *out)
{
    GError *err = NULL;
    *out = arv_device_get_integer_feature_value (dev, name, &err);
    if (err) {
        fprintf (stderr, "device_file: failed to read %s: %s\n",
                 name, err->message);
        g_clear_error (&err);
        return -1;
    }
    return 0;
}

/* Set an integer feature; return 0 on success. */
static int
set_int (ArvDevice *dev, const char *name, int64_t value)
{
    GError *err = NULL;
    arv_device_set_integer_feature_value (dev, name, value, &err);
    if (err) {
        fprintf (stderr, "device_file: failed to set %s=%" G_GINT64_FORMAT ": %s\n",
                 name, (gint64) value, err->message);
        g_clear_error (&err);
        return -1;
    }
    return 0;
}

/*
 * Get the FileAccessBuffer register node and its length.
 * Returns the ArvGcNode* or NULL on error.
 */
static ArvGcNode *
get_file_access_buffer (ArvDevice *dev, int64_t *out_buf_len)
{
    ArvGc *gc = arv_device_get_genicam (dev);
    if (!gc) {
        fprintf (stderr, "device_file: failed to get genicam object\n");
        return NULL;
    }

    ArvGcNode *node = arv_gc_get_node (gc, "FileAccessBuffer");
    if (!node) {
        fprintf (stderr, "device_file: FileAccessBuffer node not found\n");
        return NULL;
    }

    if (!ARV_IS_GC_REGISTER (node)) {
        fprintf (stderr, "device_file: FileAccessBuffer is not a register node\n");
        return NULL;
    }

    GError *err = NULL;
    guint64 len = arv_gc_register_get_length (ARV_GC_REGISTER (node), &err);
    if (err) {
        fprintf (stderr, "device_file: failed to get FileAccessBuffer length: %s\n",
                 err->message);
        g_clear_error (&err);
        return NULL;
    }

    *out_buf_len = (int64_t) len;
    return node;
}

/* ------------------------------------------------------------------ */
/*  Progress display                                                   */
/* ------------------------------------------------------------------ */

static void
print_progress (const char *verb, size_t done, size_t total,
                struct timespec *t_start)
{
    int pct = (total > 0) ? (int) (done * 100 / total) : 0;

    /* Elapsed time. */
    struct timespec now;
    clock_gettime (CLOCK_MONOTONIC, &now);
    double elapsed = (double) (now.tv_sec  - t_start->tv_sec)
                   + (double) (now.tv_nsec - t_start->tv_nsec) / 1e9;

    /* Speed in KB/s. */
    double speed_kbs = (elapsed > 0.05) ? ((double) done / 1024.0) / elapsed
                                        : 0.0;

    /* Bar: 30 chars wide. */
    int bar_fill = pct * 30 / 100;
    char bar[32];
    for (int i = 0; i < 30; i++)
        bar[i] = (i < bar_fill) ? '#' : '-';
    bar[30] = '\0';

    fprintf (stderr, "\r  %s [%s] %3d%%  %.1f MB / %.1f MB  %.0f KB/s  ",
             verb, bar, pct,
             (double) done  / (1024.0 * 1024.0),
             (double) total / (1024.0 * 1024.0),
             speed_kbs);
    fflush (stderr);
}

/*
 * Defensive close: if a previous transfer was interrupted (e.g. Ctrl-C),
 * the file may still be open on the camera.  Issuing a Close before we
 * try to Open recovers from that state.  Errors are silently ignored
 * because the file may not be open at all.
 */
static void
close_if_open (ArvDevice *dev)
{
    GError *err = NULL;
    arv_device_set_string_feature_value (dev, "FileOperationSelector",
                                         "Close", &err);
    g_clear_error (&err);
    arv_device_execute_command (dev, "FileOperationExecute", &err);
    g_clear_error (&err);
}

/*
 * Open a file for reading or writing.  Tries Open first; if it fails
 * (e.g. the camera's FileOperationSelector state was invalidated by
 * a prior info query or an interrupted transfer), re-selects the file
 * and retries.  Re-selecting FileSelector resets the camera's SFNC
 * state machine so that "Open" becomes available again.
 */
static int
file_open (ArvDevice *dev, const char *file_selector, const char *mode)
{
    if (set_str (dev, "FileOpenMode", mode) != 0)
        return -1;

    /* Optimistic try: Open directly (suppress error on failure). */
    {
        GError *err = NULL;
        arv_device_set_string_feature_value (dev, "FileOperationSelector",
                                             "Open", &err);
        if (!err) {
            arv_device_execute_command (dev, "FileOperationExecute", &err);
            if (!err)
                return 0;
        }
        g_clear_error (&err);
    }

    /* Open failed — close any stale transfer, re-select the file, retry. */
    close_if_open (dev);
    if (set_str (dev, "FileSelector", file_selector) != 0)
        return -1;
    if (set_str (dev, "FileOpenMode", mode) != 0)
        return -1;
    if (set_str (dev, "FileOperationSelector", "Open") != 0)
        return -1;
    if (exec_cmd (dev, "FileOperationExecute") != 0)
        return -1;

    return 0;
}

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

int
ag_device_file_read (ArvDevice *dev, const char *file_selector,
                     uint8_t **out_data, size_t *out_len)
{
    *out_data = NULL;
    *out_len  = 0;

    /* Select file. */
    if (set_str (dev, "FileSelector", file_selector) != 0)
        return -1;

    /* Query file size. */
    int64_t file_size = 0;
    if (get_int (dev, "FileSize", &file_size) != 0)
        return -1;

    if (file_size <= 0) {
        fprintf (stderr, "device_file: %s is empty or does not exist "
                 "(size=%" G_GINT64_FORMAT ")\n",
                 file_selector, (gint64) file_size);
        return -1;
    }

    /* Get buffer register. */
    int64_t buf_len = 0;
    ArvGcNode *buf_node = get_file_access_buffer (dev, &buf_len);
    if (!buf_node || buf_len <= 0)
        return -1;

    /* Open for reading (handles stale open from interrupted transfers). */
    if (file_open (dev, file_selector, "Read") != 0)
        return -1;

    /*
     * GOTCHA: arv_gc_register_get always reads the full register length
     * (buf_len bytes) regardless of FileAccessLength, so reading directly
     * into the output buffer would overrun on the last (short) chunk.
     * We use a scratch buffer for every register access and memcpy only
     * the bytes we actually need.  Same issue on the write path.
     */
    uint8_t *data    = g_malloc ((size_t) file_size);
    uint8_t *scratch = g_malloc ((size_t) buf_len);
    size_t  total_read = 0;

    struct timespec t_start;
    clock_gettime (CLOCK_MONOTONIC, &t_start);

    /* Set operation selector once — it stays "Read" for the entire loop. */
    if (set_str (dev, "FileOperationSelector", "Read") != 0)
        goto fail;

    int64_t prev_chunk = -1;

    /* Read loop: chunk by chunk through FileAccessBuffer. */
    while ((int64_t) total_read < file_size) {
        int64_t remaining = file_size - (int64_t) total_read;
        int64_t chunk     = (remaining < buf_len) ? remaining : buf_len;

        if (set_int (dev, "FileAccessOffset", (int64_t) total_read) != 0)
            goto fail;
        if (chunk != prev_chunk) {
            if (set_int (dev, "FileAccessLength", chunk) != 0)
                goto fail;
            prev_chunk = chunk;
        }
        if (exec_cmd (dev, "FileOperationExecute") != 0)
            goto fail;

        /* Check how many bytes were actually read. */
        int64_t result = 0;
        if (get_int (dev, "FileOperationResult", &result) != 0)
            goto fail;

        if (result <= 0)
            break;

        if (result > chunk)
            result = chunk;

        /* Read the full register into scratch, then copy what we need. */
        GError *err = NULL;
        arv_gc_register_get (ARV_GC_REGISTER (buf_node),
                             scratch, (guint64) buf_len, &err);
        if (err) {
            fprintf (stderr, "device_file: register read failed: %s\n",
                     err->message);
            g_clear_error (&err);
            goto fail;
        }

        memcpy (data + total_read, scratch, (size_t) result);
        total_read += (size_t) result;
        print_progress ("Reading", total_read, (size_t) file_size, &t_start);
    }

    fprintf (stderr, "\n");

    /* Close. */
    set_str (dev, "FileOperationSelector", "Close");
    exec_cmd (dev, "FileOperationExecute");

    g_free (scratch);
    *out_data = data;
    *out_len  = total_read;
    return 0;

fail:
    fprintf (stderr, "\n");
    set_str (dev, "FileOperationSelector", "Close");
    exec_cmd (dev, "FileOperationExecute");
    g_free (scratch);
    g_free (data);
    return -1;
}

int
ag_device_file_read_head (ArvDevice *dev, const char *file_selector,
                           size_t max_bytes,
                           uint8_t **out_data, size_t *out_len)
{
    *out_data = NULL;
    *out_len  = 0;

    if (set_str (dev, "FileSelector", file_selector) != 0)
        return -1;

    int64_t file_size = 0;
    if (get_int (dev, "FileSize", &file_size) != 0)
        return -1;

    if (file_size <= 0)
        return -1;

    /* Read at most max_bytes. */
    size_t to_read = ((size_t) file_size < max_bytes)
                     ? (size_t) file_size : max_bytes;

    int64_t buf_len = 0;
    ArvGcNode *buf_node = get_file_access_buffer (dev, &buf_len);
    if (!buf_node || buf_len <= 0)
        return -1;

    if (file_open (dev, file_selector, "Read") != 0)
        return -1;

    uint8_t *data    = g_malloc (to_read);
    uint8_t *scratch = g_malloc ((size_t) buf_len);
    size_t   total_read = 0;

    if (set_str (dev, "FileOperationSelector", "Read") != 0)
        goto fail_head;

    int64_t prev_chunk = -1;

    while (total_read < to_read) {
        int64_t remaining = (int64_t) (to_read - total_read);
        int64_t chunk     = (remaining < buf_len) ? remaining : buf_len;

        if (set_int (dev, "FileAccessOffset", (int64_t) total_read) != 0)
            goto fail_head;
        if (chunk != prev_chunk) {
            if (set_int (dev, "FileAccessLength", chunk) != 0)
                goto fail_head;
            prev_chunk = chunk;
        }
        if (exec_cmd (dev, "FileOperationExecute") != 0)
            goto fail_head;

        int64_t result = 0;
        if (get_int (dev, "FileOperationResult", &result) != 0)
            goto fail_head;

        if (result <= 0)
            break;

        if (result > chunk)
            result = chunk;

        /* Read full register into scratch, copy what we need. */
        GError *err = NULL;
        arv_gc_register_get (ARV_GC_REGISTER (buf_node),
                             scratch, (guint64) buf_len, &err);
        if (err) {
            g_clear_error (&err);
            goto fail_head;
        }

        memcpy (data + total_read, scratch, (size_t) result);
        total_read += (size_t) result;
    }

    set_str (dev, "FileOperationSelector", "Close");
    exec_cmd (dev, "FileOperationExecute");

    g_free (scratch);
    *out_data = data;
    *out_len  = total_read;
    return 0;

fail_head:
    set_str (dev, "FileOperationSelector", "Close");
    exec_cmd (dev, "FileOperationExecute");
    g_free (scratch);
    g_free (data);
    return -1;
}

int
ag_device_file_write (ArvDevice *dev, const char *file_selector,
                      const uint8_t *data, size_t len)
{
    /* Select file. */
    if (set_str (dev, "FileSelector", file_selector) != 0)
        return -1;

    /* Get buffer register. */
    int64_t buf_len = 0;
    ArvGcNode *buf_node = get_file_access_buffer (dev, &buf_len);
    if (!buf_node || buf_len <= 0)
        return -1;

    size_t n_chunks = (len + (size_t) buf_len - 1) / (size_t) buf_len;
    fprintf (stderr, "  FileAccessBuffer: %" G_GINT64_FORMAT " bytes "
             "(%zu chunks for %.1f MB)\n",
             (gint64) buf_len, n_chunks,
             (double) len / (1024.0 * 1024.0));

    /* Check free space. */
    int64_t free_space = 0;
    if (get_int (dev, "FileStorageFreeSize", &free_space) == 0) {
        /* Also account for currently used space that will be reclaimed. */
        int64_t used = 0;
        get_int (dev, "FileSize", &used);
        int64_t available = free_space + used;
        if ((int64_t) len > available) {
            fprintf (stderr, "device_file: data (%zu bytes) exceeds available "
                     "storage (%" G_GINT64_FORMAT " bytes)\n",
                     len, (gint64) available);
            return -1;
        }
    }

    /* Open for writing (handles stale open from interrupted transfers). */
    if (file_open (dev, file_selector, "Write") != 0)
        return -1;

    /* Set operation selector once — it stays "Write" for the entire loop. */
    if (set_str (dev, "FileOperationSelector", "Write") != 0)
        return -1;

    /*
     * GOTCHA (same as read path): arv_gc_register_set requires a buffer
     * of exactly buf_len bytes.  Zero-fill so the last (short) chunk
     * has deterministic padding.
     */
    uint8_t *scratch = g_malloc0 ((size_t) buf_len);

    /* Write loop. */
    size_t total_written = 0;
    int64_t prev_chunk = -1;

    struct timespec t_start;
    clock_gettime (CLOCK_MONOTONIC, &t_start);

    while (total_written < len) {
        size_t remaining = len - total_written;
        int64_t chunk = ((int64_t) remaining < buf_len)
                        ? (int64_t) remaining : buf_len;

        /* Copy data into scratch, write the full register. */
        memcpy (scratch, data + total_written, (size_t) chunk);
        GError *err = NULL;
        arv_gc_register_set (ARV_GC_REGISTER (buf_node),
                             scratch, (guint64) buf_len, &err);
        if (err) {
            fprintf (stderr, "\ndevice_file: register write failed: %s\n",
                     err->message);
            g_clear_error (&err);
            goto fail_write;
        }

        if (set_int (dev, "FileAccessOffset", (int64_t) total_written) != 0)
            goto fail_write;
        if (chunk != prev_chunk) {
            if (set_int (dev, "FileAccessLength", chunk) != 0)
                goto fail_write;
            prev_chunk = chunk;
        }
        if (exec_cmd (dev, "FileOperationExecute") != 0)
            goto fail_write;

        /* Check how many bytes were actually written. */
        int64_t result = 0;
        if (get_int (dev, "FileOperationResult", &result) != 0)
            goto fail_write;

        if (result <= 0) {
            fprintf (stderr, "\ndevice_file: write stalled at offset %zu\n",
                     total_written);
            goto fail_write;
        }

        total_written += (size_t) result;
        print_progress ("Writing", total_written, len, &t_start);
    }

    fprintf (stderr, "\n");

    /* Close. */
    set_str (dev, "FileOperationSelector", "Close");
    exec_cmd (dev, "FileOperationExecute");
    g_free (scratch);
    return 0;

fail_write:
    fprintf (stderr, "\n");
    set_str (dev, "FileOperationSelector", "Close");
    exec_cmd (dev, "FileOperationExecute");
    g_free (scratch);
    return -1;
}

int
ag_device_file_delete (ArvDevice *dev, const char *file_selector)
{
    if (set_str (dev, "FileSelector", file_selector) != 0)
        return -1;
    close_if_open (dev);

    /*
     * GOTCHA: not all cameras support the SFNC "Delete" operation.
     * The Lucid PDH016S-C doesn't expose "Delete" in its
     * FileOperationSelector enum (arv_device_set_string_feature_value
     * fails with "Value 4 not found").  Fall back to opening the file
     * for writing and immediately closing it, which truncates to zero.
     */
    {
        GError *err = NULL;
        arv_device_set_string_feature_value (dev, "FileOperationSelector",
                                             "Delete", &err);
        if (!err) {
            arv_device_execute_command (dev, "FileOperationExecute", &err);
            if (!err)
                return 0;   /* Delete succeeded. */
        }
        g_clear_error (&err);
    }

    /* Fallback: open-for-write then close → truncates to zero. */
    fprintf (stderr, "device_file: Delete not supported, "
             "truncating %s instead\n", file_selector);

    if (file_open (dev, file_selector, "Write") != 0)
        return -1;

    set_str (dev, "FileOperationSelector", "Close");
    exec_cmd (dev, "FileOperationExecute");
    return 0;
}

int
ag_device_file_info (ArvDevice *dev, const char *file_selector,
                     int64_t *out_file_size,
                     int64_t *out_storage_total,
                     int64_t *out_storage_used,
                     int64_t *out_storage_free)
{
    if (set_str (dev, "FileSelector", file_selector) != 0)
        return -1;

    int64_t v;
    if (out_file_size) {
        if (get_int (dev, "FileSize", &v) != 0) return -1;
        *out_file_size = v;
    }
    if (out_storage_total) {
        if (get_int (dev, "FileStorageSize", &v) != 0) return -1;
        *out_storage_total = v;
    }
    if (out_storage_used) {
        if (get_int (dev, "FileStorageUsedSize", &v) != 0) return -1;
        *out_storage_used = v;
    }
    if (out_storage_free) {
        if (get_int (dev, "FileStorageFreeSize", &v) != 0) return -1;
        *out_storage_free = v;
    }

    return 0;
}
