/* Real FreeType-rendered text for circuitsword-quickmenu. Falls back to
 * qm_font.c's bitmap font if FreeType init or font loading fails -- the
 * overlay must never crash or go silent just because text rendering
 * degraded. */
#include "quickmenu.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include <stdio.h>
#include <string.h>

static FT_Library qm_ft_library;
static FT_Face qm_ft_face;
static int qm_ttf_available = 0;

int qm_ttf_init(void)
{
    if (FT_Init_FreeType(&qm_ft_library) != 0) {
        fprintf(stderr, "circuitsword-quickmenu: FT_Init_FreeType failed, "
                        "falling back to bitmap font\n");
        return -1;
    }
    if (FT_New_Face(qm_ft_library, QM_TTF_FONT_PATH, 0, &qm_ft_face) != 0) {
        fprintf(stderr, "circuitsword-quickmenu: failed to load %s, "
                        "falling back to bitmap font\n", QM_TTF_FONT_PATH);
        FT_Done_FreeType(qm_ft_library);
        return -1;
    }
    qm_ttf_available = 1;
    return 0;
}

/* Fallback scale: qm_font.c's glyphs are QM_GLYPH_H (7px) tall at
 * scale 1. Pick the nearest integer scale so bitmap-font fallback text
 * is roughly the same visual height as the requested TTF px_size would
 * have been. */
static int qm_ttf_fallback_scale(int px_size)
{
    int scale = px_size / QM_GLYPH_H;
    return scale < 1 ? 1 : scale;
}

void qm_draw_text_ttf(qm_fb *fb, int x, int y, const char *text, int px_size, uint32_t color)
{
    if (!qm_ttf_available) {
        qm_draw_text(fb, x, y, text, qm_ttf_fallback_scale(px_size), color);
        return;
    }

    FT_Set_Pixel_Sizes(qm_ft_face, 0, (FT_UInt)px_size);

    uint8_t cr = (uint8_t)((color >> 16) & 0xFF);
    uint8_t cg = (uint8_t)((color >> 8) & 0xFF);
    uint8_t cb = (uint8_t)(color & 0xFF);

    int pen_x = x;
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        if (FT_Load_Char(qm_ft_face, *p, FT_LOAD_RENDER) != 0)
            continue;

        FT_GlyphSlot slot = qm_ft_face->glyph;
        FT_Bitmap *bmp = &slot->bitmap;

        int glyph_x = pen_x + slot->bitmap_left;
        int glyph_y = y + (px_size - slot->bitmap_top);

        for (unsigned int row = 0; row < bmp->rows; row++) {
            for (unsigned int col = 0; col < bmp->width; col++) {
                uint8_t a = bmp->buffer[row * (unsigned int)bmp->pitch + col];
                if (a == 0) continue;
                int px = glyph_x + (int)col;
                int py = glyph_y + (int)row;
                uint32_t bg = qm_get_pixel(fb, px, py);
                uint8_t br = (uint8_t)((bg >> 16) & 0xFF);
                uint8_t bgc = (uint8_t)((bg >> 8) & 0xFF);
                uint8_t bb = (uint8_t)(bg & 0xFF);
                uint32_t blended = QM_ARGB(255,
                    qm_alpha_blend(br, cr, a),
                    qm_alpha_blend(bgc, cg, a),
                    qm_alpha_blend(bb, cb, a));
                qm_fill_rect(fb, px, py, 1, 1, blended);
            }
        }

        pen_x += (int)(slot->advance.x >> 6);
    }
}

int qm_ttf_text_width(const char *text, int px_size)
{
    if (!qm_ttf_available)
        return qm_text_width(text, qm_ttf_fallback_scale(px_size));

    FT_Set_Pixel_Sizes(qm_ft_face, 0, (FT_UInt)px_size);

    int width = 0;
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        if (FT_Load_Char(qm_ft_face, *p, FT_LOAD_DEFAULT) != 0)
            continue;
        width += (int)(qm_ft_face->glyph->advance.x >> 6);
    }
    return width;
}
