/*
 * npy.c — minimal NumPy .npy v1.0 parser for float64 arrays
 */

/* memmem() is a GNU extension. BSD/macOS declares it in <string.h> unconditionally,
 * glibc only under _GNU_SOURCE -- without which gcc implicitly declares it as
 * returning int and truncates the pointer on 64-bit. Must precede every include. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

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

/*
 * Emit the ASCII header dict numpy itself writes, then the raw doubles.
 *
 * v1.0 requires the header be padded so that magic(6) + version(2) + len(2) + header
 * is a multiple of 64, and that it end in '\n'. numpy also emits a trailing ", " before
 * the closing brace and renders a 1-D shape as "(5,)" -- matched here so a file written
 * by this and one written by numpy.save are byte-identical for the same array.
 */
int
ag_npy_save (const char *path, const double *data, int ndim, const int *shape)
{
    if (!path || !data || !shape || ndim < 1 || ndim > 4) {
        fprintf (stderr, "npy: bad arguments to ag_npy_save\n");
        return -1;
    }

    char dims[64];
    int  n = 0;
    size_t count = 1;
    for (int i = 0; i < ndim; i++) {
        if (shape[i] < 0) {
            fprintf (stderr, "npy: negative dimension %d\n", shape[i]);
            return -1;
        }
        count *= (size_t) shape[i];
        n += snprintf (dims + n, sizeof dims - (size_t) n, "%s%d",
                       i ? ", " : "", shape[i]);
        if (n < 0 || (size_t) n >= sizeof dims)
            return -1;
    }
    if (ndim == 1)      /* numpy writes 1-D shapes as "(5,)" */
        n += snprintf (dims + n, sizeof dims - (size_t) n, ",");

    char header[256];
    int hlen = snprintf (header, sizeof header,
                         "{'descr': '<f8', 'fortran_order': False, 'shape': (%s), }", dims);
    if (hlen < 0 || (size_t) hlen >= sizeof header)
        return -1;

    /* Pad with spaces so the whole preamble is 64-byte aligned, then newline. */
    const int preamble = (int) sizeof k_npy_magic + 2;          /* magic+version, len field */
    int total = preamble + hlen + 1;
    int pad = (64 - (total % 64)) % 64;
    if (hlen + pad + 1 >= (int) sizeof header)
        return -1;
    memset (header + hlen, ' ', (size_t) pad);
    header[hlen + pad] = '\n';
    const uint16_t header_len = (uint16_t) (hlen + pad + 1);

    FILE *f = fopen (path, "wb");
    if (!f) {
        fprintf (stderr, "npy: cannot write %s\n", path);
        return -1;
    }
    const uint8_t lenbytes[2] = { (uint8_t) (header_len & 0xff),
                                  (uint8_t) (header_len >> 8) };   /* little-endian */
    int ok = fwrite (k_npy_magic, 1, sizeof k_npy_magic, f) == sizeof k_npy_magic
          && fwrite (lenbytes, 1, 2, f) == 2
          && fwrite (header, 1, header_len, f) == header_len
          && (count == 0 || fwrite (data, sizeof (double), count, f) == count);
    if (fclose (f) != 0)
        ok = 0;
    if (!ok) {
        fprintf (stderr, "npy: short write to %s\n", path);
        return -1;
    }
    return 0;
}
