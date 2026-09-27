#ifndef CODEMAP_FONTS_H
#define CODEMAP_FONTS_H

/* The app's fonts: Inter (regular/bold/italic) and JetBrains Mono, compiled
 * into the binary (see cmake/embed_file.cmake) and baked into Nuklear's
 * atlas at several sizes.
 *
 * Every size is baked at `logical px * framebuffer scale` and then
 * reports its logical height, so text is rasterized at the display's real
 * resolution (crisp on Retina) while all layout stays in logical pixels
 * like the rest of the UI. */

struct nk_font_atlas;
struct nk_user_font;

typedef enum {
    FONT_REGULAR,
    FONT_BOLD,
    FONT_ITALIC,
    FONT_MONO,
    FONT_STYLE_COUNT
} FontStyle;

/* Call between nk_glfw3_font_stash_begin and _end. fb_scale: framebuffer
 * pixels per logical pixel (2 on Retina). */
void fonts_add(struct nk_font_atlas *atlas, float fb_scale);

/* Call right after nk_glfw3_font_stash_end (baking fills in each font's
 * metrics, which this then converts back to logical pixels). */
void fonts_finish(void);

/* Panel text and the file preview. */
const struct nk_user_font *fonts_ui(void);
const struct nk_user_font *fonts_mono(void);

/* `style` at any logical pixel height, for zoomable canvas text: the
 * smallest baked size at or above px (or the largest one), scaled to px.
 * Nuklear keeps a pointer to the font in each draw command, so the
 * returned font lives in a per-frame pool -- valid until the next
 * fonts_frame_reset(). */
const struct nk_user_font *fonts_sized(FontStyle style, float px);
void fonts_frame_reset(void);

#endif
