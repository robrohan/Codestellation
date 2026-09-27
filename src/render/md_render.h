#ifndef CODEMAP_MD_RENDER_H
#define CODEMAP_MD_RENDER_H

/* A deliberately small markdown renderer for canvas boxes, drawing
 * straight into a Nuklear command buffer at any zoom.
 *
 * Supported: `#`/`##`/`###` headings, `- `/`* ` bullets, blank-line
 * paragraph breaks, **bold**, *italic*, `code`, and [[links]] (shown
 * without brackets, in the link colour). Everything else is plain text.
 * `_` is never emphasis, so snake_case identifiers survive. Text wraps
 * at word boundaries. */

struct nk_command_buffer;
struct nk_rect;

/* Lays out and draws `markdown` inside r (screen space, already inset for
 * padding). zoom scales every font size and spacing (1.0 = 14px body
 * text). Drawing stops once it runs past the bottom of r; the caller
 * scissors to the box. */
void md_render_draw(struct nk_command_buffer *canvas, struct nk_rect r, const char *markdown, float zoom);

/* The box's title only -- its first non-blank line with markdown markers
 * stripped -- as one bold line at px, for zoom levels where full text
 * would be unreadable. */
void md_render_title(struct nk_command_buffer *canvas, struct nk_rect r, const char *markdown, float px);

#endif
