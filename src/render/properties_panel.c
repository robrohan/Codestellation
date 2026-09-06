#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#include "nuklear.h"

#include "properties_panel.h"
#include "tinyfiledialogs.h"
#include "../common/pathutil.h"
#include <stddef.h>

char *properties_panel_draw(struct nk_context *ctx, int *show_origin, PanelRect *out_bounds) {
    char *picked = NULL;

    if (nk_begin(ctx, PROPERTIES_PANEL_TITLE, nk_rect(20, 20, 260, 150),
                 NK_WINDOW_BORDER | NK_WINDOW_TITLE | NK_WINDOW_MOVABLE | NK_WINDOW_SCALABLE |
                 NK_WINDOW_MINIMIZABLE)) {
        struct nk_rect b = nk_window_get_bounds(ctx);
        if (nk_window_is_collapsed(ctx, PROPERTIES_PANEL_TITLE)) b.h = PANEL_HEADER_HEIGHT;
        out_bounds->x = b.x;
        out_bounds->y = b.y;
        out_bounds->w = b.w;
        out_bounds->h = b.h;

        nk_layout_row_dynamic(ctx, 26, 1);
        if (nk_button_label(ctx, "Open...")) {
            /* Blocking, modal (shells out to osascript on macOS) -- fine
             * to call straight from widget code, same as any other
             * synchronous button action here. NULL means canceled. */
            const char *dir = tinyfd_selectFolderDialog("Open Project Directory", NULL);
            if (dir) picked = xstrdup(dir);
        }

        nk_layout_row_dynamic(ctx, 24, 1);
        nk_checkbox_label(ctx, "Show origin", show_origin);
    } else {
        out_bounds->x = out_bounds->y = out_bounds->w = out_bounds->h = 0.0f;
    }
    nk_end(ctx);

    return picked;
}
