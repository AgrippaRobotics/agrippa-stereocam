/*
 * npy.h — minimal NumPy .npy v1.0 parser for float64 arrays
 *
 * Parses the .npy binary format (magic, header dict, raw data) and
 * returns the shape and a g_malloc'd copy of the float64 payload.
 * Only supports dtype '<f8' (little-endian float64), C-order, up to 2D.
 */

#ifndef AG_NPY_H
#define AG_NPY_H

#include <glib.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    int      ndim;
    int      shape[4];
    double  *data;         /* g_malloc'd; caller must g_free */
    size_t   n_elements;
} AgNpyArray;

/*
 * Parse an in-memory .npy buffer.
 * On success, fills *out and returns 0.  out->data is newly allocated.
 * On error, prints a diagnostic and returns -1.
 */
int ag_npy_load (const uint8_t *buf, size_t len, AgNpyArray *out);

#endif /* AG_NPY_H */
