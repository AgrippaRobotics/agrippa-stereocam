/*
 * overlay.h — Software drawing primitives for RGB24 pixel buffers
 *
 * Used by the capture pipeline to draw tag boundary overlays onto
 * saved images without an SDL dependency.
 */

#ifndef AG_OVERLAY_H
#define AG_OVERLAY_H

#include <glib.h>
#include <stdint.h>

/* Draw a line from (x0,y0) to (x1,y1) into an RGB24 buffer.
 * Uses Bresenham's line algorithm.  Clips to buffer bounds. */
void ag_overlay_line_rgb (guint8 *rgb, guint width, guint height,
                          int x0, int y0, int x1, int y1,
                          uint8_t r, uint8_t g, uint8_t b);

/* Draw a quadrilateral (4 connected line segments) into an RGB24 buffer. */
void ag_overlay_quad_rgb (guint8 *rgb, guint width, guint height,
                          const double pts[4][2],
                          uint8_t r, uint8_t g, uint8_t b);

/* Render a text string into an RGB24 buffer using the 5x7 bitmap font. */
void ag_overlay_text_rgb (guint8 *rgb, guint width, guint height,
                          const char *text, int x, int y, int scale,
                          uint8_t r, uint8_t g, uint8_t b);

#ifdef HAVE_APRILTAG
#include "apriltag_detect.h"

/* Draw tag overlays (quad + ID label) for an array of detected tags. */
void ag_overlay_tags_rgb (guint8 *rgb, guint width, guint height,
                          const AgTagOverlay *tags, int n_tags,
                          uint8_t r, uint8_t g, uint8_t b);
#endif

#endif /* AG_OVERLAY_H */
