/*
 * device_file.h — GenICam SFNC FileAccessControl via Aravis
 *
 * Read, write, delete, and query files stored on the camera's persistent
 * UserFile storage (up to 16 MB on the PDH016S).
 *
 * Implementation gotchas (see device_file.c for details):
 *
 * 1. Scratch buffer for register access:
 *    arv_gc_register_get/set always reads/writes the FULL register length
 *    (e.g. 65536 bytes) regardless of the FileAccessLength you requested.
 *    Direct reads into the output buffer will overrun on the last (short)
 *    chunk.  All read/write paths use a scratch buffer sized to the full
 *    register length, then memcpy only the bytes we need.
 *
 * 2. FileOperationSelector="Delete" not supported:
 *    The Lucid PDH016S-C does not expose "Delete" in its
 *    FileOperationSelector enum.  ag_device_file_delete() falls back
 *    to opening the file for writing and immediately closing it, which
 *    truncates the file to zero bytes.
 */

#ifndef AG_DEVICE_FILE_H
#define AG_DEVICE_FILE_H

#include <arv.h>
#include <stdint.h>
#include <stddef.h>

/*
 * Read the entire contents of a camera user file into a heap buffer.
 * Caller must g_free(*out_data) when done.
 * Returns 0 on success, -1 on error (prints diagnostics).
 */
int ag_device_file_read (ArvDevice *dev, const char *file_selector,
                         uint8_t **out_data, size_t *out_len);

/*
 * Write a buffer to the camera's user file, replacing any existing content.
 * Returns 0 on success, -1 on error (prints diagnostics).
 */
int ag_device_file_write (ArvDevice *dev, const char *file_selector,
                          const uint8_t *data, size_t len);

/*
 * Delete a user file from the camera.  The camera must be power-cycled
 * after deletion for the change to take full effect.
 * Returns 0 on success, -1 on error (prints diagnostics).
 */
int ag_device_file_delete (ArvDevice *dev, const char *file_selector);

/*
 * Read up to max_bytes from the beginning of a camera user file.
 * Useful for reading a fixed-size header without downloading the
 * entire file.  Caller must g_free(*out_data) when done.
 * Returns 0 on success, -1 on error.
 */
int ag_device_file_read_head (ArvDevice *dev, const char *file_selector,
                               size_t max_bytes,
                               uint8_t **out_data, size_t *out_len);

/*
 * Query storage information for a user file slot.
 * Any output pointer may be NULL if that field is not needed.
 * Returns 0 on success, -1 on error.
 */
int ag_device_file_info (ArvDevice *dev, const char *file_selector,
                         int64_t *out_file_size,
                         int64_t *out_storage_total,
                         int64_t *out_storage_used,
                         int64_t *out_storage_free);

#endif /* AG_DEVICE_FILE_H */
