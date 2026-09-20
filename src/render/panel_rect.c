#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#include "nuklear.h"

#include "panel_rect.h"

float panel_header_height(struct nk_context *ctx) {
    return ctx->style.font->height +
           2.0f * ctx->style.window.header.padding.y +
           2.0f * ctx->style.window.header.label_padding.y;
}
