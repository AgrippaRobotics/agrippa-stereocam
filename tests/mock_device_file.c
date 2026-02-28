/*
 * mock_device_file.c — configurable mock for device_file.h
 *
 * Provides stub implementations for all ag_device_file_* functions.
 * ag_device_file_read is configurable: tests inject data and a return
 * code before exercising the code under test.
 *
 * All other functions (write, delete, read_head, info) return -1.
 */

#include "mock_device_file.h"
#include "../src/device_file.h"

#include <glib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/*  Internal state                                                     */
/* ------------------------------------------------------------------ */

static uint8_t *mock_read_data = NULL;
static size_t   mock_read_len  = 0;
static int      mock_read_rc   = 0;
static int      mock_read_calls = 0;

/* ------------------------------------------------------------------ */
/*  Configuration API                                                  */
/* ------------------------------------------------------------------ */

void
mock_device_file_reset (void)
{
    g_free (mock_read_data);
    mock_read_data  = NULL;
    mock_read_len   = 0;
    mock_read_rc    = 0;
    mock_read_calls = 0;
}

void
mock_device_file_set_read_data (const uint8_t *data, size_t len)
{
    g_free (mock_read_data);
    if (data && len > 0) {
        mock_read_data = g_malloc (len);
        memcpy (mock_read_data, data, len);
        mock_read_len = len;
    } else {
        mock_read_data = NULL;
        mock_read_len  = 0;
    }
}

void
mock_device_file_set_read_rc (int rc)
{
    mock_read_rc = rc;
}

int
mock_device_file_read_call_count (void)
{
    return mock_read_calls;
}

/* ------------------------------------------------------------------ */
/*  Mock implementations                                               */
/* ------------------------------------------------------------------ */

int
ag_device_file_read (ArvDevice *dev, const char *file_selector,
                     uint8_t **out_data, size_t *out_len)
{
    (void) dev;
    (void) file_selector;

    mock_read_calls++;

    if (mock_read_rc != 0)
        return mock_read_rc;

    if (mock_read_data && mock_read_len > 0) {
        *out_data = g_malloc (mock_read_len);
        memcpy (*out_data, mock_read_data, mock_read_len);
        *out_len = mock_read_len;
    } else {
        *out_data = NULL;
        *out_len  = 0;
    }

    return 0;
}

int
ag_device_file_write (ArvDevice *dev, const char *file_selector,
                      const uint8_t *data, size_t len)
{
    (void) dev; (void) file_selector;
    (void) data; (void) len;
    return -1;
}

int
ag_device_file_delete (ArvDevice *dev, const char *file_selector)
{
    (void) dev; (void) file_selector;
    return -1;
}

int
ag_device_file_read_head (ArvDevice *dev, const char *file_selector,
                           size_t max_bytes,
                           uint8_t **out_data, size_t *out_len)
{
    (void) dev; (void) file_selector;
    (void) max_bytes; (void) out_data; (void) out_len;
    return -1;
}

int
ag_device_file_info (ArvDevice *dev, const char *file_selector,
                     int64_t *out_file_size,
                     int64_t *out_storage_total,
                     int64_t *out_storage_used,
                     int64_t *out_storage_free)
{
    (void) dev; (void) file_selector;
    (void) out_file_size; (void) out_storage_total;
    (void) out_storage_used; (void) out_storage_free;
    return -1;
}
