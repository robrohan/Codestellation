#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#include "nuklear.h"

#include "properties_panel.h"
#include "winstate.h"
#include "tinyfiledialogs.h"
#include "../common/pathutil.h"
#include <stddef.h>
#include <stdlib.h>

/* COCOMO settings for the open project. true if any changed this frame. */
static bool draw_cost_settings(struct nk_context *ctx, CostParams *cost) {
    CostParams before = *cost;
    static const char *modes[COCOMO_MODE_COUNT] = { "Organic", "Semi-detached", "Embedded" };

    nk_layout_row_dynamic(ctx, 20, 1);
    nk_label(ctx, "Cost estimate (COCOMO)", NK_TEXT_LEFT);
    nk_layout_row_dynamic(ctx, 24, 1);
    /* Drag steps: $100 per pixel, 0.01 overhead per pixel. */
    nk_property_double(ctx, "#Salary/year:", 0.0, &cost->salary, 10000000.0, 1000.0, 100.0f);
    nk_property_double(ctx, "#Overhead x:", 0.1, &cost->overhead, 10.0, 0.1, 0.01f);
    cost->mode = (CocomoMode)nk_combo(ctx, modes, COCOMO_MODE_COUNT, (int)cost->mode, 24, nk_vec2(200, 100));

    nk_layout_row_dynamic(ctx, 20, 1);
    nk_label(ctx, "If written by AI", NK_TEXT_LEFT);
    nk_layout_row_dynamic(ctx, 24, 1);
    double share = cost->ai_share * 100.0;
    nk_property_double(ctx, "#AI share %:", 0.0, &share, 100.0, 5.0, 0.5f);
    cost->ai_share = share / 100.0;
    nk_property_double(ctx, "#Price/M tokens:", 0.0, &cost->token_price, 1000.0, 0.5, 0.05f);
    nk_property_double(ctx, "#Reasoning x:", 1.0, &cost->reasoning_multiplier, 100000.0, 10.0, 1.0f);

    return before.salary != cost->salary || before.overhead != cost->overhead || before.mode != cost->mode ||
           before.ai_share != cost->ai_share || before.token_price != cost->token_price ||
           before.reasoning_multiplier != cost->reasoning_multiplier;
}

PropsResult properties_panel_draw(struct nk_context *ctx, int *show_origin, bool has_notes, bool has_project,
                                  CostParams *cost, bool *out_cost_changed, bool *out_export_clicked,
                                  PanelRect *out_bounds) {
    PropsResult result = { PROPS_NONE, NULL, NULL };
    *out_export_clicked = false;
    *out_cost_changed = false;

    float h = 220.0f + (has_notes ? 30.0f : 0.0f) + (has_project ? 64.0f : 0.0f) + (cost ? 228.0f : 0.0f);
    if (winstate_begin(ctx, PROPERTIES_PANEL_TITLE, 20, 20, 260, h,
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
#if defined(__APPLE__)
            /* No filter on macOS: tinyfd turns "*.json" into osascript's
             * `choose file of type {"json"}`, and `of type` wants a type
             * identifier (public.json), not an extension -- so every file
             * came up greyed out. main.c checks what was picked. */
            const char *file = tinyfd_openFileDialog("Open Project (project.json)", "", 0, NULL, NULL, 0);
#else
            const char *patterns[1] = { "*.json" };
            const char *file = tinyfd_openFileDialog("Open Project (project.json)", "", 1, patterns,
                                                     "Codestellation project", 0);
#endif
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
        if (cost) *out_cost_changed = draw_cost_settings(ctx, cost);

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
