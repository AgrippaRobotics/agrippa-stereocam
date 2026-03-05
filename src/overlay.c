/*
 * overlay.c — Software drawing primitives for RGB24 pixel buffers
 */

#include "overlay.h"
#include "font_data.h"

#include <stdio.h>
#include <stdlib.h>

/* Set a single RGB pixel, with bounds checking. */
static inline void
set_pixel (guint8 *rgb, guint width, guint height,
           int x, int y, uint8_t r, uint8_t g, uint8_t b)
{
    if (x < 0 || x >= (int) width || y < 0 || y >= (int) height)
        return;
    size_t off = ((size_t) y * width + (size_t) x) * 3;
    rgb[off]     = r;
    rgb[off + 1] = g;
    rgb[off + 2] = b;
}

void
ag_overlay_line_rgb (guint8 *rgb, guint width, guint height,
                     int x0, int y0, int x1, int y1,
                     uint8_t r, uint8_t g, uint8_t b)
{
    int dx = abs (x1 - x0);
    int dy = abs (y1 - y0);
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;

    for (;;) {
        set_pixel (rgb, width, height, x0, y0, r, g, b);
        if (x0 == x1 && y0 == y1)
            break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 <  dx) { err += dx; y0 += sy; }
    }
}

void
ag_overlay_quad_rgb (guint8 *rgb, guint width, guint height,
                     const double pts[4][2],
                     uint8_t r, uint8_t g, uint8_t b)
{
    for (int i = 0; i < 4; i++) {
        int ni = (i + 1) % 4;
        ag_overlay_line_rgb (rgb, width, height,
                             (int) (pts[i][0]  + 0.5),
                             (int) (pts[i][1]  + 0.5),
                             (int) (pts[ni][0] + 0.5),
                             (int) (pts[ni][1] + 0.5),
                             r, g, b);
    }
}

void
ag_overlay_text_rgb (guint8 *rgb, guint width, guint height,
                     const char *text, int x, int y, int scale,
                     uint8_t r, uint8_t g, uint8_t b)
{
    int cx = x;
    for (const char *p = text; *p; p++) {
        int idx = ag_font_index (*p);
        if (idx < 0) {
            cx += 6 * scale;
            continue;
        }

        const uint8_t *glyph = ag_font_glyphs[idx];
        for (int row = 0; row < 7; row++) {
            uint8_t bits = glyph[row];
            for (int col = 0; col < 5; col++) {
                if (bits & (0x80 >> col)) {
                    for (int sy = 0; sy < scale; sy++)
                        for (int sx = 0; sx < scale; sx++)
                            set_pixel (rgb, width, height,
                                       cx + col * scale + sx,
                                       y  + row * scale + sy,
                                       r, g, b);
                }
            }
        }
        cx += 6 * scale;
    }
}

#ifdef HAVE_APRILTAG

void
ag_overlay_tags_rgb (guint8 *rgb, guint width, guint height,
                     const AgTagOverlay *tags, int n_tags,
                     uint8_t r, uint8_t g, uint8_t b)
{
    for (int i = 0; i < n_tags; i++) {
        ag_overlay_quad_rgb (rgb, width, height, tags[i].p, r, g, b);

        char label[16];
        snprintf (label, sizeof label, "%d", tags[i].id);
        int lx = (int) (tags[i].center[0] + 0.5);
        int ly = (int) (tags[i].center[1] + 0.5) - 10;
        ag_overlay_text_rgb (rgb, width, height, label, lx, ly, 1, r, g, b);
    }
}

#endif /* HAVE_APRILTAG */
