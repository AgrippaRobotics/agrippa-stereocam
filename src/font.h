/*
 * font.h — Minimal 5x7 bitmap font for SDL2 overlay (header-only)
 *
 * Glyph data and index function live in font_data.h (no SDL dependency).
 * This header adds the SDL2 renderer function.
 */

#ifndef AG_FONT_H
#define AG_FONT_H

#include "font_data.h"

#include <SDL2/SDL.h>

/*
 * Render a text string onto an SDL renderer using the bitmap font.
 * Each pixel is drawn as a (scale x scale) filled rectangle.
 * Glyph spacing: 6 x scale pixels (5 glyph + 1 gap).
 */
static inline void
ag_font_render (SDL_Renderer *renderer, const char *text,
                int x, int y, int scale,
                uint8_t r, uint8_t g, uint8_t b)
{
    SDL_SetRenderDrawColor (renderer, r, g, b, 255);

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
                    SDL_Rect rc = {
                        cx + col * scale,
                        y  + row * scale,
                        scale, scale
                    };
                    SDL_RenderFillRect (renderer, &rc);
                }
            }
        }
        cx += 6 * scale;
    }
}

#endif /* AG_FONT_H */
