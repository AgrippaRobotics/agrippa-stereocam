/*
 * mock_device_file.h — configurable mock for device_file.h
 *
 * Provides the same symbols as device_file.c so that calib_load.o
 * (and any other module that references ag_device_file_*) can link
 * without a real camera.
 *
 * Tests configure mock behaviour before each call:
 *
 *   mock_device_file_reset ();
 *   mock_device_file_set_read_data (buf, len);
 *   // ... call code under test ...
 *   TEST_ASSERT_EQUAL_INT (1, mock_device_file_read_call_count ());
 */

#ifndef MOCK_DEVICE_FILE_H
#define MOCK_DEVICE_FILE_H

#include <stddef.h>
#include <stdint.h>

/* Reset all mock state (call counts, injected data, return codes). */
void mock_device_file_reset (void);

/*
 * Configure what ag_device_file_read returns.
 * The mock copies `data` internally so the caller can free it after.
 * Pass NULL / 0 to make ag_device_file_read return an empty buffer.
 */
void mock_device_file_set_read_data (const uint8_t *data, size_t len);

/* Set the return code for ag_device_file_read (default: 0 = success). */
void mock_device_file_set_read_rc (int rc);

/* Return how many times ag_device_file_read was called since reset. */
int mock_device_file_read_call_count (void);

#endif /* MOCK_DEVICE_FILE_H */
