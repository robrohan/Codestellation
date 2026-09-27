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
#include <stdlib.h>

PropsResult properties_panel_draw(struct nk_context *ctx, int *show_origin, bool has_notes, bool has_project,
                                  bool *out_export_clicked, PanelRect *out_bounds) {
    PropsResult result = { PROPS_NONE, NULL, NULL };
    *out_export_clicked = false;

    float h = 220.0f + (has_notes ? 30.0f : 0.0f) + (has_project ? 64.0f : 0.0f);
    if (nk_begin(ctx, PROPERTIES_PANEL_TITLE, nk_rect(20, 20, 260, h),
                 NK_WINDOW_BORDER | NK_WINDOW_TITLE | NK_WINDOW_MOVABLE | NK_WINDOW_SCALABLE |
                 NK_WINDOW_MINIMIZABLE)) {
        struct nk_rect b = nk_window_get_bounds(ctx);
        out_bounds->x = b.x;
        out_bounds->y = b.y;
        out_bounds->w = b.w;
        out_bounds->h = b.h;

        /* The dialogs below are blocking and modal (osascript on macOS) --
         * fine to call straight from widget code, same as any other
         * synchronous button action here. NULL means canceled. */
        nk_layout_row_dynamic(ctx, 26, 1);
        if (nk_button_label(ctx, "New Project...")) {
            const char *dir = tinyfd_selectFolderDialog("Choose a folder for the new project", NULL);
            if (dir) {
                char *d = xstrdup(dir);
                /* A non-NULL default makes this a text prompt (NULL would
                 * make it a password box). */
                const char *title = tinyfd_inputBox("New Project", "Project title:", "");
                if (title) {
                    result.action = PROPS_NEW_PROJECT;
                    result.path = d;
                    result.title = xstrdup(title);
                } else {
                    free(d);
                }
            }
        }
        if (nk_button_label(ctx, "Open Project...")) {
            const char *patterns[1] = { "*.json" };
            const char *file = tinyfd_openFileDialog("Open Project (project.json)", "", 1, patterns,
                                                     "Codestellation project", 0);
            if (file) {
                result.action = PROPS_OPEN_PROJECT;
                result.path = xstrdup(file);
            }
        }
        if (nk_button_label(ctx, "Open Folder...")) {
            const char *dir = tinyfd_selectFolderDialog("Open Project Directory", NULL);
            if (dir) {
                result.action = PROPS_OPEN_FOLDER;
                result.path = xstrdup(dir);
            }
        }

        if (has_project) {
            nk_layout_row_dynamic(ctx, 26, 1);
            if (nk_button_label(ctx, "Export Manual...")) result.action = PROPS_EXPORT_MANUAL;
            if (nk_button_label(ctx, "Export LLM Brief...")) result.action = PROPS_EXPORT_LLM;
        }

        nk_layout_row_dynamic(ctx, 24, 1);
        nk_checkbox_label(ctx, "Show origin", show_origin);

        /* Notes live under Application Support -- easy to lose track of,
         * hence a direct way to get a copy somewhere you'll find it.
         * Hidden entirely rather than shown disabled when there's nothing
         * to export yet (no project loaded). */
        if (has_notes) {
            nk_layout_row_dynamic(ctx, 26, 1);
            if (nk_button_label(ctx, "Export Notes...")) *out_export_clicked = true;
        }
    } else if (nk_window_is_collapsed(ctx, PROPERTIES_PANEL_TITLE)) {
        /* nk_begin returns false while MINIMIZED -- see the matching
         * comment in ui_panel_draw for the full explanation and why
         * nk_window_get_bounds is still safe to call here. */
        struct nk_rect b = nk_window_get_bounds(ctx);
        out_bounds->x = b.x;
        out_bounds->y = b.y;
        out_bounds->w = b.w;
        out_bounds->h = panel_header_height(ctx);
    } else {
        out_bounds->x = out_bounds->y = out_bounds->w = out_bounds->h = 0.0f;
    }
    nk_end(ctx);

    return result;
}
