#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#include "nuklear.h"

#include "ui_panel.h"
#include "note_compose.h"
#include "fonts.h"
#include "textarea.h"
#include "winstate.h"
#include "../common/pathutil.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Cache the last-loaded file so it isn't re-read from disk every frame. */
static char *g_cached_path = NULL;
static char *g_cached_content = NULL;

/* The file view: a read-only text area with line numbers and a marker on
 * each line that has a note (textarea.h). It keeps its own scroll and
 * cursor, so "+ Add note" seeds the line the user clicked, and switching
 * focus to a button doesn't lose the place. Reloaded whenever
 * g_content_gen moves on (a different file loaded). */
static TextArea g_view;
static bool g_view_ready = false;
static unsigned g_content_gen = 0;
static unsigned g_view_gen = (unsigned)-1;

/* Two-step delete confirmation for the notes list -- see draw_note_row.
 * Reset whenever the selection context changes so an armed "Confirm?" on
 * one file's note doesn't silently carry over and land on the wrong note
 * if the user switches selection and clicks Delete again in the same
 * screen position. */
static char g_pending_delete_id[16] = "";
static char g_delete_arm_context[512] = "";

static void reset_delete_arm_if_context_changed(const char *context) {
    if (strcmp(g_delete_arm_context, context) != 0) {
        g_pending_delete_id[0] = '\0';
        strncpy(g_delete_arm_context, context, sizeof(g_delete_arm_context) - 1);
        g_delete_arm_context[sizeof(g_delete_arm_context) - 1] = '\0';
    }
}

/* Old Mac files end lines with a lone '\r'; the text area only breaks on
 * '\n' (and draws '\r' as nothing, so CRLF is fine as it is), so turn a
 * lone '\r' into '\n' to keep those files' lines apart. Caller frees. */
static char *normalize_line_endings(const char *src) {
    size_t len = strlen(src);
    char *out = (char *)malloc(len + 1);
    for (size_t i = 0; i < len; i++) out[i] = (src[i] == '\r' && src[i + 1] != '\n') ? '\n' : src[i];
    out[len] = '\0';
    return out;
}

static void load_file_if_needed(const char *path) {
    if (path && g_cached_path && strcmp(g_cached_path, path) == 0) return;

    free(g_cached_path);
    free(g_cached_content);
    g_cached_path = NULL;
    g_cached_content = NULL;
    g_content_gen++;
    if (!path) return;

    g_cached_path = xstrdup(path);

    FILE *f = fopen(path, "rb");
    if (!f) {
        g_cached_content = xstrdup("(could not read file)");
        return;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0) {
        fclose(f);
        g_cached_content = xstrdup("(could not read file)");
        return;
    }
    char *buf = (char *)malloc((size_t)size + 1);
    size_t got = fread(buf, 1, (size_t)size, f);
    buf[got] = '\0';
    fclose(f);
    g_cached_content = normalize_line_endings(buf);
    free(buf);
}

/* Line (1-based) containing the rune at `rune_offset` in `content`. Walks
 * runes rather than bytes -- nk_edit's cursor is a codepoint count (see
 * nk_utf_len in nuklear.h), so a byte-offset count would drift on any file
 * with multi-byte UTF-8 content (e.g. in a comment) before the cursor.
 * '\n' is always a lead byte on its own (ASCII), so it's unambiguous to
 * spot while walking. */
/* One note's summary line + body preview + Edit/Delete. Edit opens the
 * separate Note compose pane directly (safe mid-list, it only copies the
 * note's fields into that pane's own state). Delete is deferred via
 * *delete_target instead -- acting on it immediately would call
 * notes_delete_note, which frees/reloads the very `notes->notes` array
 * this loop is iterating pointers into. */
static void draw_note_row(struct nk_context *ctx, const Note *note, const Note **delete_target) {
    nk_layout_row_dynamic(ctx, 16, 1);
    if (note->path) {
        if (note->has_line) nk_labelf(ctx, NK_TEXT_LEFT, "[%s] line %d \xc2\xb7 %s", note->id, note->line, note->updated);
        else nk_labelf(ctx, NK_TEXT_LEFT, "[%s] whole file \xc2\xb7 %s", note->id, note->updated);
    } else {
        nk_labelf(ctx, NK_TEXT_LEFT, "[%s] group, %zu files \xc2\xb7 %s", note->id, note->group_path_count, note->updated);
    }
    /* The whole body, wrapped (a little narrower than the row, so the
     * height is never short). */
    float body_w = nk_window_get_content_region(ctx).w - 12.0f;
    nk_layout_row_dynamic(ctx, textarea_label_height(ctx, note->body, body_w), 1);
    textarea_label(ctx, note->body);

    nk_layout_row_dynamic(ctx, 20, 2);
    if (nk_button_label(ctx, "Edit")) note_compose_open_edit(note);
    bool pending = strcmp(g_pending_delete_id, note->id) == 0;
    if (nk_button_label(ctx, pending ? "Confirm?" : "Delete")) {
        if (pending) {
            *delete_target = note;
        } else {
            /* First click arms this row; clicking a different row's
             * Delete overwrites the pending id, re-arming there instead --
             * effectively "clicking anything else clears/moves it". */
            strncpy(g_pending_delete_id, note->id, sizeof(g_pending_delete_id) - 1);
            g_pending_delete_id[sizeof(g_pending_delete_id) - 1] = '\0';
        }
    }
}

static void draw_group_mode(struct nk_context *ctx, const char **cluster_paths, size_t cluster_count,
                             NoteSet *notes, const char *notes_md_path) {
    char context[512];
    snprintf(context, sizeof(context), "group:%zu:%s", cluster_count,
             cluster_count > 0 ? cluster_paths[0] : "");
    reset_delete_arm_if_context_changed(context);

    nk_layout_row_dynamic(ctx, 20, 1);
    nk_labelf(ctx, NK_TEXT_LEFT, "%zu files selected", cluster_count);

    nk_layout_row_dynamic(ctx, 100, 1);
    if (nk_group_begin(ctx, "cluster_paths", NK_WINDOW_BORDER)) {
        for (size_t i = 0; i < cluster_count; i++) {
            nk_layout_row_dynamic(ctx, 18, 1);
            nk_label(ctx, cluster_paths[i], NK_TEXT_LEFT);
        }
        nk_group_end(ctx);
    }

    const Note *found[64];
    size_t found_n = notes_find_for_group(notes, cluster_paths, cluster_count, found, 64);

    nk_layout_row_dynamic(ctx, 20, 1);
    nk_labelf(ctx, NK_TEXT_LEFT, "Notes (%zu)", found_n);

    const Note *delete_target = NULL;
    nk_layout_row_dynamic(ctx, 150, 1);
    if (nk_group_begin(ctx, "group_notes", NK_WINDOW_BORDER)) {
        for (size_t i = 0; i < found_n; i++) draw_note_row(ctx, found[i], &delete_target);
        nk_group_end(ctx);
    }
    if (delete_target) {
        notes_delete_note(notes_md_path, notes, delete_target->id);
        g_pending_delete_id[0] = '\0';
    }

    nk_layout_row_dynamic(ctx, 26, 1);
    if (nk_button_label(ctx, "+ Add group note")) {
        note_compose_open_add(cluster_paths, cluster_count, false, 1);
    }
}

/* ---- Stats section ------------------------------------------------- */

#define STATS_ROW_H 16.0f
#define STATS_MAX_CYCLE_NAMES 4

static enum nk_collapse_states g_stats_state = NK_MAXIMIZED;

static const char *base_name(const char *path) {
    const char *slash = strrchr(path, '/');
    const char *bslash = strrchr(path, '\\');
    if (slash && (!bslash || slash > bslash)) return slash + 1;
    return bslash ? bslash + 1 : path;
}

/* Other files in the selected file's cycle (up to `cap`), and how many
 * there are in all. */
static int cycle_mates(const Graph *g, int selected, const char **out, int cap) {
    int id = g->nodes[selected].stats.cycle_id, n = 0;
    if (id < 0) return 0;
    for (size_t i = 0; i < g->node_count; i++) {
        if ((int)i == selected || g->nodes[i].stats.cycle_id != id) continue;
        if (n < cap) out[n] = base_name(g->nodes[i].path);
        n++;
    }
    return n;
}

/* Rows the section will draw below its header -- so the file viewer under
 * it can be given exactly the height that's left. */
static int stats_row_count(const Graph *g, int selected) {
    const NodeStats *st = &g->nodes[selected].stats;
    if (!st->has_stats) return 2;
    int rows = 11; /* lines, indent, complexity, in, out, blast, cycle, unresolved, parse errors, git, hotspot */
    if (st->git_last_commit > 0) rows++;
    if (st->cycle_size > 1) {
        int mates = st->cycle_size - 1;
        rows += mates > STATS_MAX_CYCLE_NAMES ? STATS_MAX_CYCLE_NAMES + 1 : mates;
    }
    return rows;
}

static void stat_row(struct nk_context *ctx, const char *label, const char *value) {
    static const float ratio[] = { 0.38f, 0.62f };
    nk_layout_row(ctx, NK_DYNAMIC, STATS_ROW_H, 2, ratio);
    nk_label(ctx, label, NK_TEXT_LEFT);
    nk_label(ctx, value, NK_TEXT_LEFT);
}

static const char *plural(int n, const char *one, const char *many) {
    return n == 1 ? one : many;
}

static void draw_stats(struct nk_context *ctx, const Graph *g, int selected) {
    const NodeStats *st = &g->nodes[selected].stats;
    char v[160];
    if (!st->has_stats) {
        nk_layout_row_dynamic(ctx, STATS_ROW_H, 1);
        nk_label(ctx, "This graph was built before stats existed.", NK_TEXT_LEFT);
        nk_label(ctx, "Open the folder again to rebuild it.", NK_TEXT_LEFT);
        return;
    }

    snprintf(v, sizeof(v), "%d (%d blank, %d with comments)", st->lines, st->blank_lines, st->comment_lines);
    stat_row(ctx, "Lines", v);
    snprintf(v, sizeof(v), "%d %s deep", st->max_indent, plural(st->max_indent, "level", "levels"));
    stat_row(ctx, "Indentation", v);
    if (st->complexity < 0) {
        snprintf(v, sizeof(v), "n/a for %s", g->nodes[selected].language);
    } else if (st->functions > 0 && st->hot_count > 0) {
        snprintf(v, sizeof(v), "%d (%d %s, worst %d: %s, line %d)", st->complexity, st->functions,
                 plural(st->functions, "function", "functions"), st->hot[0].complexity,
                 st->hot[0].name[0] ? st->hot[0].name : "anonymous", st->hot[0].line);
    } else if (st->functions > 0) {
        snprintf(v, sizeof(v), "%d (%d %s, worst %d)", st->complexity, st->functions,
                 plural(st->functions, "function", "functions"), st->max_function_complexity);
    } else {
        snprintf(v, sizeof(v), "%d", st->complexity);
    }
    stat_row(ctx, "Complexity", v);

    snprintf(v, sizeof(v), "%d %s", st->fan_in, plural(st->fan_in, "file", "files"));
    stat_row(ctx, "Depended on by", v);
    snprintf(v, sizeof(v), "%d %s", st->fan_out, plural(st->fan_out, "file", "files"));
    stat_row(ctx, "Depends on", v);
    if (st->blast_radius < 0) snprintf(v, sizeof(v), "not computed (graph too large)");
    else snprintf(v, sizeof(v), "%d %s, directly or not", st->blast_radius, plural(st->blast_radius, "file", "files"));
    stat_row(ctx, "Blast radius", v);

    if (st->cycle_size > 1) {
        const char *names[STATS_MAX_CYCLE_NAMES];
        int mates = cycle_mates(g, selected, names, STATS_MAX_CYCLE_NAMES);
        snprintf(v, sizeof(v), "with %d other %s:", mates, plural(mates, "file", "files"));
        stat_row(ctx, "In a cycle", v);
        int shown = mates < STATS_MAX_CYCLE_NAMES ? mates : STATS_MAX_CYCLE_NAMES;
        for (int i = 0; i < shown; i++) stat_row(ctx, "", names[i]);
        if (mates > shown) {
            snprintf(v, sizeof(v), "and %d more", mates - shown);
            stat_row(ctx, "", v);
        }
    } else {
        stat_row(ctx, "In a cycle", "no");
    }

    snprintf(v, sizeof(v), "%d", st->unresolved);
    stat_row(ctx, "Unresolved refs", v);
    if (st->parse_errors > 0) snprintf(v, sizeof(v), "%d (some links may be missing)", st->parse_errors);
    else snprintf(v, sizeof(v), "0");
    stat_row(ctx, "Parse errors", v);

    if (st->git_commits < 0) {
        snprintf(v, sizeof(v), "not in a git repo");
    } else if (st->git_commits == 0) {
        snprintf(v, sizeof(v), "never committed");
    } else {
        snprintf(v, sizeof(v), "%d %s by %d %s", st->git_commits, plural(st->git_commits, "commit", "commits"),
                 st->git_authors, plural(st->git_authors, "author", "authors"));
    }
    stat_row(ctx, "Git history", v);
    if (st->git_last_commit > 0) {
        time_t t = (time_t)st->git_last_commit;
        struct tm *tm = localtime(&t);
        if (!tm || strftime(v, sizeof(v), "%Y-%m-%d", tm) == 0) snprintf(v, sizeof(v), "?");
        stat_row(ctx, "Last commit", v);
    }

    if (st->hotspot_rank > 0) {
        int ranked = 0;
        for (size_t i = 0; i < g->node_count; i++) if (g->nodes[i].stats.hotspot_rank > 0) ranked++;
        snprintf(v, sizeof(v), "#%d of %d (commits x complexity)", st->hotspot_rank, ranked);
    } else {
        snprintf(v, sizeof(v), "not ranked");
    }
    stat_row(ctx, "Hotspot", v);
}

/* ---- Duplicate-code bars ------------------------------------------------- */

#define DUP_MARKERS_MAX 32
#define DUP_TIP_PLACES 12

/* One copied stretch of the selected file, and where else it is. */
typedef struct {
    int line, end;
    int other, other_line, other_end;
} DupPlace;

static int cmp_dup_place(const void *pa, const void *pb) {
    const DupPlace *a = (const DupPlace *)pa, *b = (const DupPlace *)pb;
    if (a->line != b->line) return a->line < b->line ? -1 : 1;
    return (a->end > b->end) - (a->end < b->end);
}

/* Length of the folder prefix every file in the graph shares, so the
 * tooltip shows "src/x.c" rather than the full path. */
static size_t common_dir_len(const Graph *g) {
    if (g->node_count == 0) return 0;
    const char *first = g->nodes[0].path;
    size_t n = strlen(first);
    for (size_t i = 1; i < g->node_count && n > 0; i++) {
        const char *p = g->nodes[i].path;
        size_t k = 0;
        while (k < n && p[k] && p[k] == first[k]) k++;
        n = k;
    }
    while (n > 0 && first[n - 1] != '/' && first[n - 1] != '\\') n--;
    return n;
}

/* Fills markers with a bar per copied stretch of the selected file
 * (overlapping stretches merged), each tipped with the other places the
 * code appears. Returns how many were added. */
static int duplicate_markers(const Graph *g, int selected, TextAreaMarker *markers, int cap) {
    static char tips[DUP_MARKERS_MAX][1024];
    static DupPlace *places = NULL;
    static size_t places_cap = 0;

    size_t n = 0;
    for (size_t i = 0; i < g->clone_count; i++) {
        const GraphClone *c = &g->clones[i];
        for (int side = 0; side < 2; side++) {
            if ((side == 0 ? c->a : c->b) != selected) continue;
            if (n == places_cap) {
                places_cap = places_cap ? places_cap * 2 : 64;
                places = (DupPlace *)realloc(places, places_cap * sizeof(DupPlace));
            }
            places[n++] = side == 0 ? (DupPlace){ c->a_line, c->a_end, c->b, c->b_line, c->b_end }
                                    : (DupPlace){ c->b_line, c->b_end, c->a, c->a_line, c->a_end };
        }
    }
    if (n == 0) return 0;
    qsort(places, n, sizeof(DupPlace), cmp_dup_place);
    size_t strip = common_dir_len(g);

    int count = 0;
    if (cap > DUP_MARKERS_MAX) cap = DUP_MARKERS_MAX;
    for (size_t s = 0; s < n && count < cap;) {
        size_t e = s + 1;
        int end = places[s].end;
        while (e < n && places[e].line <= end) {
            if (places[e].end > end) end = places[e].end;
            e++;
        }
        char *tip = tips[count];
        size_t cap_b = sizeof(tips[count]);
        size_t len = (size_t)snprintf(tip, cap_b, "Lines %d-%d are also in:", places[s].line, end);
        int listed = 0, more = 0;
        for (size_t k = s; k < e; k++) {
            bool seen = false;
            for (size_t j = s; j < k && !seen; j++) {
                seen = places[j].other == places[k].other && places[j].other_line == places[k].other_line &&
                       places[j].other_end == places[k].other_end;
            }
            if (seen) continue;
            if (listed == DUP_TIP_PLACES) { more++; continue; }
            const char *path = places[k].other == selected ? "this file" : g->nodes[places[k].other].path + strip;
            if (len < cap_b) {
                len += (size_t)snprintf(tip + len, cap_b - len, "\n  %s:%d-%d", path, places[k].other_line,
                                        places[k].other_end);
            }
            listed++;
        }
        if (more && len < cap_b) snprintf(tip + len, cap_b - len, "\n  ...and %d more", more);
        markers[count++] = (TextAreaMarker){ places[s].line, -1, TEXTAREA_MARKER_BAR, tip, end };
        s = e;
    }
    return count;
}

static void draw_single_mode(struct nk_context *ctx, float panel_h, const Graph *graph, int selected,
                              const char *selected_path, const char *selected_language,
                              NoteSet *notes, const char *notes_md_path) {
    load_file_if_needed(selected_path);
    reset_delete_arm_if_context_changed(selected_path ? selected_path : "");

    if (!selected_path) {
        nk_layout_row_dynamic(ctx, 20, 1);
        nk_label(ctx, "Codestellation", NK_TEXT_LEFT);
        nk_label(ctx, "Right-click a node to select it.", NK_TEXT_LEFT);
        nk_label(ctx, "Left-drag a node to reposition it.", NK_TEXT_LEFT);
        nk_label(ctx, "Ctrl/Cmd+right-click to add to a group.", NK_TEXT_LEFT);
        return;
    }

    nk_layout_row_dynamic(ctx, 20, 1);
    nk_label(ctx, selected_language ? selected_language : "", NK_TEXT_LEFT);
    float path_w = nk_window_get_content_region(ctx).w - 12.0f;
    float path_h = textarea_label_height(ctx, selected_path, path_w);
    nk_layout_row_dynamic(ctx, path_h, 1);
    textarea_label(ctx, selected_path);

    /* Stats, collapsible; its height comes out of the file viewer's. */
    float stats_h = 0.0f;
    bool have_node = graph && selected >= 0 && (size_t)selected < graph->node_count;
    if (have_node) {
        float pitch = STATS_ROW_H + ctx->style.window.spacing.y;
        stats_h = 24.0f + ctx->style.window.spacing.y;
        if (nk_tree_state_push(ctx, NK_TREE_TAB, "Stats", &g_stats_state)) {
            draw_stats(ctx, graph, selected);
            stats_h += pitch * (float)stats_row_count(graph, selected);
            nk_tree_pop(ctx);
        }
    }

    /* Reserve fixed heights for everything below the content viewer
     * (notes label, notes list, the add-note button), same "whatever's
     * left" approach as before -- just a much smaller reservation now
     * that the compose box lives in its own pane instead of here. The
     * 140 base (rather than the raw ~110 the fixed rows above actually
     * sum to) is deliberate slack: nuklear adds a small gap between each
     * stacked row, and with this many rows that adds up to more than it
     * looks like -- too little slack here was pushing total content a
     * few px past the window, triggering this window's own scrollbar for
     * completely ordinary-sized content (nothing actually needing to
     * scroll). Better to keep a bit of daylight than fight that again. */
    const float notes_label_h = 20.0f, notes_list_h = 150.0f, add_button_h = 30.0f;
    const float reserved = 100.0f + path_h + stats_h + notes_label_h + notes_list_h + add_button_h;
    float content_h = panel_h - reserved;
    if (content_h < 60.0f) content_h = 60.0f;

    if (!g_view_ready) {
        textarea_init(&g_view, TEXTAREA_GUTTER | TEXTAREA_MONO);
        g_view_ready = true;
    }
    if (g_view_gen != g_content_gen) {
        textarea_set_text(&g_view, g_cached_content ? g_cached_content : "");
        g_view_gen = g_content_gen;
    }

    const Note *found[64];
    size_t found_n = notes_find_for_path(notes, selected_path, found, 64);

    /* Gutter markers: a dot on every line with a note (id = index into
     * found, clickable), a ring around the start of each of the file's
     * most complex functions, and a bar beside code that's copied
     * elsewhere in the project (neither clickable; tooltip says why). */
    TextAreaMarker markers[64 + STATS_HOT_MAX + DUP_MARKERS_MAX];
    static char hot_tips[STATS_HOT_MAX][96];
    int marker_n = 0;
    for (size_t i = 0; i < found_n; i++) {
        if (found[i]->has_line) {
            markers[marker_n++] = (TextAreaMarker){ found[i]->line, (int)i, TEXTAREA_MARKER_DOT, NULL };
        }
    }
    if (graph && selected >= 0 && (size_t)selected < graph->node_count) {
        const NodeStats *st = &graph->nodes[selected].stats;
        for (int k = 0; k < st->hot_count; k++) {
            snprintf(hot_tips[k], sizeof(hot_tips[k]), "%s: complexity %d",
                     st->hot[k].name[0] ? st->hot[k].name : "anonymous function", st->hot[k].complexity);
            markers[marker_n++] = (TextAreaMarker){ st->hot[k].line, -1, TEXTAREA_MARKER_RING, hot_tips[k] };
        }
        marker_n += duplicate_markers(graph, selected, markers + marker_n, DUP_MARKERS_MAX);
    }

    nk_layout_row_dynamic(ctx, content_h, 1);
    TextAreaResult view;
    textarea_draw(ctx, &g_view, markers, marker_n, &view);
    if (view.marker_click_id >= 0) {
        note_compose_open_edit(found[view.marker_click_id]);
    } else if (view.gutter_click_line > 0) {
        const char *p[1] = { selected_path };
        note_compose_open_add(p, 1, true, view.gutter_click_line);
    }

    nk_layout_row_dynamic(ctx, notes_label_h, 1);
    nk_labelf(ctx, NK_TEXT_LEFT, "Notes (%zu)", found_n);

    const Note *delete_target = NULL;
    nk_layout_row_dynamic(ctx, notes_list_h, 1);
    if (nk_group_begin(ctx, "file_notes", NK_WINDOW_BORDER)) {
        for (size_t i = 0; i < found_n; i++) draw_note_row(ctx, found[i], &delete_target);
        nk_group_end(ctx);
    }
    if (delete_target) {
        notes_delete_note(notes_md_path, notes, delete_target->id);
        g_pending_delete_id[0] = '\0';
    }

    nk_layout_row_dynamic(ctx, add_button_h, 1);
    if (nk_button_label(ctx, "+ Add note")) {
        const char *p[1] = { selected_path };
        int line = textarea_cursor_line(&g_view);
        note_compose_open_add(p, 1, line > 0, line);
    }
}

void ui_panel_draw(struct nk_context *ctx, int window_width, int window_height,
                    const Graph *graph, int selected,
                    const char *selected_path, const char *selected_language,
                    const char **cluster_paths, size_t cluster_count,
                    NoteSet *notes, const char *notes_md_path,
                    PanelRect *out_bounds) {
    float w = (float)UI_PANEL_WIDTH;
    /* NK_WINDOW_MOVABLE|NK_WINDOW_SCALABLE: this rect is only honored the
     * frame the window is first created (nuklear owns its position/size
     * from then on, updated by the user's own drag/resize), and only until
     * the user moves it -- winstate remembers it from then on, across
     * canvas trips and restarts. So this is just a first-launch default. */
    if (winstate_begin(ctx, UI_PANEL_TITLE, (float)window_width - w, 0, w, (float)window_height,
                       NK_WINDOW_BORDER | NK_WINDOW_TITLE | NK_WINDOW_MOVABLE | NK_WINDOW_SCALABLE |
                       NK_WINDOW_MINIMIZABLE)) {
        struct nk_rect b = nk_window_get_bounds(ctx);
        out_bounds->x = b.x;
        out_bounds->y = b.y;
        out_bounds->w = b.w;
        out_bounds->h = b.h;

        if (cluster_count > 1) {
            draw_group_mode(ctx, cluster_paths, cluster_count, notes, notes_md_path);
        } else {
            struct nk_vec2 size = nk_window_get_size(ctx);
            draw_single_mode(ctx, size.y, graph, selected, selected_path, selected_language, notes, notes_md_path);
        }
    } else if (nk_window_is_collapsed(ctx, UI_PANEL_TITLE)) {
        /* nk_begin/nk_panel_begin returns false for a MINIMIZED window,
         * not just a hidden/closed one (confirmed in nuklear.h: `return
         * !(layout->flags & NK_WINDOW_HIDDEN) && !(layout->flags &
         * NK_WINDOW_MINIMIZED);`) -- so this whole branch, not just the
         * "if collapsed" line within it, used to be skipped while shaded,
         * leaving out_bounds zeroed. panel_rect_contains treats w<=0 as
         * "not open, nothing to avoid", so a shaded panel was invisible to
         * the 3D-interaction gate entirely: dragging its still-visible,
         * still-draggable header (nuklear draws and moves it regardless of
         * this return value) also orbited/panned the 3D view underneath in
         * the same gesture. ctx->current is still this window here (set
         * unconditionally in nk_begin_titled before the collapse check
         * that decides the return value), so nk_window_get_bounds is still
         * safe to call -- just override its height to the real header
         * strip, matching the shaded window's actual visible/draggable
         * extent, same as the expanded branch would if it could see this
         * state. */
        struct nk_rect b = nk_window_get_bounds(ctx);
        out_bounds->x = b.x;
        out_bounds->y = b.y;
        out_bounds->w = b.w;
        out_bounds->h = panel_header_height(ctx);
    } else {
        out_bounds->x = out_bounds->y = out_bounds->w = out_bounds->h = 0.0f;
    }
    nk_end(ctx);
}

void ui_panel_shutdown(void) {
    free(g_cached_path);
    free(g_cached_content);
    g_cached_path = NULL;
    g_cached_content = NULL;
    if (g_view_ready) textarea_free(&g_view);
    g_view_ready = false;
}
