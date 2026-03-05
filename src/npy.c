/*
 * npy.c — minimal NumPy .npy v1.0 parser for float64 arrays
 */

#include "npy.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* .npy v1.0 magic: \x93NUMPY + major(1) + minor(0). */
static const uint8_t k_npy_magic[8] = {
    0x93, 'N', 'U', 'M', 'P', 'Y', 0x01, 0x00
};

/*
 * Scan the ASCII header dict for a key and return the start of its value.
 * Example: for key "shape" in "{'descr': '<f8', 'shape': (3, 3), }"
 * returns pointer to "(3, 3), }".
 * Returns NULL if not found.
 */
static const char *
find_value (const char *header, size_t header_len, const char *key)
{
    char pattern[64];
    int n = snprintf (pattern, sizeof pattern, "'%s':", key);
    if (n < 0 || (size_t) n >= sizeof pattern)
        return NULL;

    const char *p = memmem (header, header_len, pattern, (size_t) n);
    if (!p)
        return NULL;

    p += n;
    /* Skip whitespace after colon. */
    while (p < header + header_len && *p == ' ')
        p++;
    return p;
}

/*
 * Parse a shape tuple like "(3, 3)" or "(1, 14)" from the header.
 * Fills shape[] and returns ndim, or -1 on error.
 */
static int
parse_shape (const char *val, int *shape)
{
    if (*val != '(')
        return -1;
    val++;

    int ndim = 0;
    while (ndim < 4) {
        /* Skip whitespace. */
        while (*val == ' ')
            val++;

        if (*val == ')')
            break;

        char *end = NULL;
        long v = strtol (val, &end, 10);
        if (end == val || v <= 0)
            return -1;
        shape[ndim++] = (int) v;
        val = end;

        /* Skip optional comma and whitespace. */
        while (*val == ',' || *val == ' ')
            val++;
    }

    return ndim;
}

int
ag_npy_load (const uint8_t *buf, size_t len, AgNpyArray *out)
{
    memset (out, 0, sizeof *out);

    /* Validate magic + version (v1.0). */
    if (len < 10 || memcmp (buf, k_npy_magic, 8) != 0) {
        fprintf (stderr, "npy: bad magic or version\n");
        return -1;
    }

    /* Header length is a 2-byte LE uint at offset 8. */
    uint16_t header_len = (uint16_t) buf[8] | ((uint16_t) buf[9] << 8);
    size_t data_offset = 10 + header_len;

    if (data_offset > len) {
        fprintf (stderr, "npy: header length %u exceeds buffer\n", header_len);
        return -1;
    }

    const char *header = (const char *) (buf + 10);

    /* Verify dtype is '<f8' (little-endian float64). */
    const char *descr = find_value (header, header_len, "descr");
    if (!descr || strncmp (descr, "'<f8'", 5) != 0) {
        fprintf (stderr, "npy: unsupported dtype (expected '<f8')\n");
        return -1;
    }

    /* Verify C-order (not Fortran). */
    const char *order = find_value (header, header_len, "fortran_order");
    if (!order || strncmp (order, "False", 5) != 0) {
        fprintf (stderr, "npy: unsupported fortran_order (expected False)\n");
        return -1;
    }

    /* Parse shape. */
    const char *shape_val = find_value (header, header_len, "shape");
    if (!shape_val) {
        fprintf (stderr, "npy: missing 'shape' in header\n");
        return -1;
    }

    int shape[4] = {0};
    int ndim = parse_shape (shape_val, shape);
    if (ndim <= 0) {
        fprintf (stderr, "npy: failed to parse shape\n");
        return -1;
    }

    size_t n_elements = 1;
    for (int i = 0; i < ndim; i++)
        n_elements *= (size_t) shape[i];

    size_t data_bytes = n_elements * sizeof (double);
    if (data_offset + data_bytes > len) {
        fprintf (stderr, "npy: data truncated (need %zu bytes, have %zu)\n",
                 data_bytes, len - data_offset);
        return -1;
    }

    /* Copy data to a new buffer. */
    double *data = g_malloc (data_bytes);
    memcpy (data, buf + data_offset, data_bytes);

    out->ndim       = ndim;
    out->n_elements = n_elements;
    out->data       = data;
    for (int i = 0; i < ndim; i++)
        out->shape[i] = shape[i];

    return 0;
}
