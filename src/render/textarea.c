/* The shared wrapped text box -- see textarea.h.
 *
 * Layout: each logical line is greedily word-wrapped into visual rows at
 * the text width. Runs of spaces always stay on the row they started on
 * (they "hang" past the edge, as in most editors), and a single word wider
 * than the row is broken between characters. Rows are cached and rebuilt
 * only when the text, width or font changes, and only visible rows are
 * drawn, so a large file costs one layout pass per resize, not per frame.
 *
 * Everything is in byte offsets into UTF-8 text, moved by whole code
 * points. Widths come from the Nuklear font, with '\t' as four spaces and
 * '\r' as nothing.
 */

#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#include "nuklear.h"

#include "textarea.h"
#include "fonts.h"
#include "theme.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TA_PAD 4.0f
#define TA_SCROLLBAR_W 8.0f
#define TA_TAB_SPACES 4
#define TA_UNDO_MAX 200
#define TA_UNDO_MAX_BYTES (256 * 1024)
#define TA_REPEAT_DELAY 0.45f
#define TA_REPEAT_RATE 0.035f

enum { EDIT_NONE, EDIT_TYPE, EDIT_BACKSPACE, EDIT_DELETE, EDIT_OTHER };

static TextArea *g_focused = NULL;

/* ---- Text measuring ------------------------------------------------ */

static float font_width(const struct nk_user_font *f, const char *s, size_t n) {
    return n ? f->width(f->userdata, f->height, s, (int)n) : 0.0f;
}

/* Width of s[0..n), with tabs as spaces and carriage returns invisible. */
static float run_width(const struct nk_user_font *f, const char *s, size_t n) {
    float w = 0.0f;
    size_t a = 0;
    for (size_t i = 0; i <= n; i++) {
        if (i == n || s[i] == '\t' || s[i] == '\r') {
            w += font_width(f, s + a, i - a);
            if (i < n && s[i] == '\t') w += TA_TAB_SPACES * font_width(f, " ", 1);
            a = i + 1;
        }
    }
    return w;
}

static void draw_run(struct nk_command_buffer *canvas, const struct nk_user_font *f, float x, float y, float h,
                     const char *s, size_t n, struct nk_color fg) {
    size_t a = 0;
    for (size_t i = 0; i <= n; i++) {
        if (i == n || s[i] == '\t' || s[i] == '\r') {
            if (i > a) {
                float w = font_width(f, s + a, i - a);
                nk_draw_text(canvas, nk_rect(x, y, w + 1.0f, h), s + a, (int)(i - a), f, nk_rgba(0, 0, 0, 0), fg);
                x += w;
            }
            if (i < n && s[i] == '\t') x += TA_TAB_SPACES * font_width(f, " ", 1);
            a = i + 1;
        }
    }
}

/* ---- UTF-8 steps ---------------------------------------------------- */

static size_t next_cp(const char *s, size_t len, size_t i) {
    if (i >= len) return len;
    i++;
    while (i < len && ((unsigned char)s[i] & 0xC0) == 0x80) i++;
    return i;
}

static size_t prev_cp(const char *s, size_t i) {
    if (i == 0) return 0;
    i--;
    while (i > 0 && ((unsigned char)s[i] & 0xC0) == 0x80) i--;
    return i;
}

static bool is_space(char c) { return c == ' ' || c == '\t' || c == '\r'; }

static bool is_word(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
           ((unsigned char)c & 0x80);
}

/* ---- Layout ----------------------------------------------------------- */

static void push_row(TextAreaRow **rows, size_t *count, size_t *cap, size_t start, size_t end, int line) {
    if (*count == *cap) {
        *cap = *cap ? *cap * 2 : 64;
        *rows = (TextAreaRow *)realloc(*rows, *cap * sizeof(TextAreaRow));
    }
    (*rows)[*count].start = start;
    (*rows)[*count].end = end;
    (*rows)[*count].line = line;
    (*count)++;
}

static void wrap_line(const struct nk_user_font *f, const char *t, size_t ls, size_t le, int line, float width,
                      TextAreaRow **rows, size_t *count, size_t *cap) {
    if (width <= 0.0f || run_width(f, t + ls, le - ls) <= width) {
        push_row(rows, count, cap, ls, le, line);
        return;
    }
    size_t row_start = ls, i = ls, pushed = 0;
    float x = 0.0f;
    while (i < le) {
        bool sp = is_space(t[i]);
        size_t j = i;
        while (j < le && is_space(t[j]) == sp) j++;
        float tw = run_width(f, t + i, j - i);
        if (sp || x + tw <= width) {
            x += tw;
            i = j;
            continue;
        }
        if (x > 0.0f) { /* start the word on a fresh row */
            push_row(rows, count, cap, row_start, i, line);
            pushed++;
            row_start = i;
            x = 0.0f;
            continue;
        }
        /* One word wider than the row: break it between characters. */
        size_t k = i;
        float cx = 0.0f;
        while (k < j) {
            size_t nk = next_cp(t, le, k);
            float cw = run_width(f, t + k, nk - k);
            if (cx + cw > width && k > i) break;
            cx += cw;
            k = nk;
        }
        if (k >= le) break;
        push_row(rows, count, cap, row_start, k, line);
        pushed++;
        row_start = k;
        i = k;
        x = 0.0f;
    }
    if (row_start < le || pushed == 0) push_row(rows, count, cap, row_start, le, line);
}

/* Wraps `text` into rows; returns the logical line count. */
static int layout_text(const struct nk_user_font *f, const char *text, size_t len, float width,
                       TextAreaRow **rows, size_t *count, size_t *cap) {
    *count = 0;
    int line = 1;
    size_t i = 0;
    for (;;) {
        size_t le = i;
        while (le < len && text[le] != '\n') le++;
        wrap_line(f, text, i, le, line, width, rows, count, cap);
        if (le >= len) break;
        i = le + 1;
        line++;
    }
    return line;
}

static void ensure_layout(TextArea *ta, const struct nk_user_font *f, float width) {
    if (!ta->layout_dirty && ta->layout_font == f && fabsf(ta->layout_width - width) < 0.5f) return;
    ta->line_count = layout_text(f, ta->text, ta->len, width, &ta->rows, &ta->row_count, &ta->row_cap);
    ta->layout_font = f;
    ta->layout_width = width;
    ta->layout_dirty = false;
}

/* The row the byte offset sits on: the last row starting at or before it,
 * so a position at a soft wrap belongs to the row that follows. */
static size_t row_of(const TextArea *ta, size_t pos) {
    size_t lo = 0, hi = ta->row_count;
    while (hi - lo > 1) {
        size_t mid = (lo + hi) / 2;
        if (ta->rows[mid].start <= pos) lo = mid;
        else hi = mid;
    }
    return lo;
}

/* Byte offset in row r nearest to x (relative to the text's left edge). */
static size_t offset_at_x(const TextArea *ta, const struct nk_user_font *f, size_t r, float x) {
    const TextAreaRow *row = &ta->rows[r];
    size_t k = row->start;
    float acc = 0.0f;
    while (k < row->end) {
        size_t nk = next_cp(ta->text, row->end, k);
        float cw = run_width(f, ta->text + k, nk - k);
        if (x < acc + cw * 0.5f) return k;
        acc += cw;
        k = nk;
    }
    return row->end;
}

static float x_of(const TextArea *ta, const struct nk_user_font *f, size_t pos) {
    const TextAreaRow *row = &ta->rows[row_of(ta, pos)];
    size_t end = pos < row->end ? pos : row->end;
    return run_width(f, ta->text + row->start, end - row->start);
}

/* ---- Text storage and editing ---------------------------------------- */

static void reserve(TextArea *ta, size_t need) {
    if (need + 1 <= ta->cap) return;
    size_t cap = ta->cap ? ta->cap : 64;
    while (cap < need + 1) cap *= 2;
    ta->text = (char *)realloc(ta->text, cap);
    ta->cap = cap;
}

static size_t sel_lo(const TextArea *ta) { return ta->cursor < ta->anchor ? ta->cursor : ta->anchor; }
static size_t sel_hi(const TextArea *ta) { return ta->cursor < ta->anchor ? ta->anchor : ta->cursor; }
static bool has_sel(const TextArea *ta) { return ta->cursor != ta->anchor; }

static void replace_range(TextArea *ta, size_t a, size_t b, const char *s, size_t n) {
    size_t new_len = ta->len - (b - a) + n;
    reserve(ta, new_len);
    memmove(ta->text + a + n, ta->text + b, ta->len - b + 1);
    if (n) memcpy(ta->text + a, s, n);
    ta->len = new_len;
    ta->layout_dirty = true;
}

static void snapshot_free(TextAreaSnapshot *s) { free(s->text); }

static void stack_push(TextAreaSnapshot **stack, size_t *count, size_t *cap, const TextArea *ta) {
    if (*count == TA_UNDO_MAX) {
        snapshot_free(&(*stack)[0]);
        memmove(*stack, *stack + 1, (*count - 1) * sizeof(TextAreaSnapshot));
        (*count)--;
    }
    if (*count == *cap) {
        *cap = *cap ? *cap * 2 : 16;
        *stack = (TextAreaSnapshot *)realloc(*stack, *cap * sizeof(TextAreaSnapshot));
    }
    TextAreaSnapshot *s = &(*stack)[(*count)++];
    s->text = (char *)malloc(ta->len + 1);
    memcpy(s->text, ta->text, ta->len + 1);
    s->cursor = ta->cursor;
}

static void clear_stack(TextAreaSnapshot *stack, size_t *count) {
    for (size_t i = 0; i < *count; i++) snapshot_free(&stack[i]);
    *count = 0;
}

/* Records the state before an edit. Runs of typing (or of deleting)
 * collapse into one undo step. */
static void record_undo(TextArea *ta, int kind) {
    if (ta->len > TA_UNDO_MAX_BYTES) return;
    if (kind != EDIT_OTHER && kind == ta->last_edit_kind) return;
    stack_push(&ta->undo, &ta->undo_count, &ta->undo_cap, ta);
    clear_stack(ta->redo, &ta->redo_count);
    ta->last_edit_kind = kind;
}

static bool restore(TextArea *ta, TextAreaSnapshot *from, size_t *from_count, TextAreaSnapshot **to,
                    size_t *to_count, size_t *to_cap) {
    if (*from_count == 0) return false;
    stack_push(to, to_count, to_cap, ta);
    TextAreaSnapshot *s = &from[--(*from_count)];
    size_t n = strlen(s->text);
    reserve(ta, n);
    memcpy(ta->text, s->text, n + 1);
    ta->len = n;
    ta->cursor = ta->anchor = s->cursor <= n ? s->cursor : n;
    snapshot_free(s);
    ta->layout_dirty = true;
    ta->last_edit_kind = EDIT_NONE;
    return true;
}

static void insert(TextArea *ta, const char *s, size_t n, int kind) {
    record_undo(ta, kind);
    size_t a = sel_lo(ta), b = sel_hi(ta);
    replace_range(ta, a, b, s, n);
    ta->cursor = ta->anchor = a + n;
}

static void delete_range(TextArea *ta, size_t a, size_t b, int kind) {
    if (a >= b) return;
    record_undo(ta, kind);
    replace_range(ta, a, b, "", 0);
    ta->cursor = ta->anchor = a;
}

/* ---- Public state API ------------------------------------------------- */

void textarea_init(TextArea *ta, unsigned flags) {
    memset(ta, 0, sizeof(*ta));
    ta->flags = flags;
    reserve(ta, 0);
    ta->text[0] = '\0';
    ta->line_count = 1;
    ta->layout_dirty = true;
}

void textarea_free(TextArea *ta) {
    if (g_focused == ta) g_focused = NULL;
    clear_stack(ta->undo, &ta->undo_count);
    clear_stack(ta->redo, &ta->redo_count);
    free(ta->undo);
    free(ta->redo);
    free(ta->rows);
    free(ta->text);
    memset(ta, 0, sizeof(*ta));
}

void textarea_set_text(TextArea *ta, const char *text) {
    size_t n = text ? strlen(text) : 0;
    reserve(ta, n);
    if (n) memcpy(ta->text, text, n);
    ta->text[n] = '\0';
    ta->len = n;
    ta->cursor = ta->anchor = 0;
    ta->cursor_placed = false;
    ta->scroll_y = 0.0f;
    ta->pref_x_valid = false;
    ta->dragging_text = ta->dragging_bar = false;
    ta->layout_dirty = true;
    ta->last_edit_kind = EDIT_NONE;
    clear_stack(ta->undo, &ta->undo_count);
    clear_stack(ta->redo, &ta->redo_count);
}

const char *textarea_text(const TextArea *ta) { return ta->text ? ta->text : ""; }

void textarea_focus(TextArea *ta) { ta->want_focus = true; }

void textarea_blur(TextArea *ta) {
    ta->want_focus = false;
    ta->focused = false;
    ta->dragging_text = ta->dragging_bar = false;
    if (g_focused == ta) g_focused = NULL;
}

int textarea_cursor_line(const TextArea *ta) {
    if (!ta->cursor_placed) return 0;
    int line = 1;
    for (size_t i = 0; i < ta->cursor && i < ta->len; i++) if (ta->text[i] == '\n') line++;
    return line;
}

static void set_focus(TextArea *ta, bool on) {
    if (on) {
        if (g_focused && g_focused != ta) g_focused->focused = false;
        g_focused = ta;
    } else if (g_focused == ta) {
        g_focused = NULL;
    }
    ta->focused = on;
}

/* ---- Keyboard ----------------------------------------------------------- */

static bool key_hit(TextArea *ta, struct nk_context *ctx, enum nk_keys k) {
    if (nk_input_is_key_pressed(&ctx->input, k)) {
        ta->repeat_key = (int)k;
        ta->repeat_wait = TA_REPEAT_DELAY;
        return true;
    }
    if (ta->repeat_key == (int)k && nk_input_is_key_down(&ctx->input, k)) {
        ta->repeat_wait -= ctx->delta_time_seconds;
        if (ta->repeat_wait <= 0.0f) {
            ta->repeat_wait = TA_REPEAT_RATE;
            return true;
        }
    }
    return false;
}

static void move_to(TextArea *ta, size_t pos, bool extend, bool keep_pref) {
    ta->cursor = pos;
    if (!extend) ta->anchor = pos;
    if (!keep_pref) ta->pref_x_valid = false;
    ta->cursor_placed = true;
    ta->scroll_to_cursor = true;
    ta->last_edit_kind = EDIT_NONE;
}

static size_t word_left(const TextArea *ta, size_t i) {
    while (i > 0 && !is_word(ta->text[prev_cp(ta->text, i)])) i = prev_cp(ta->text, i);
    while (i > 0 && is_word(ta->text[prev_cp(ta->text, i)])) i = prev_cp(ta->text, i);
    return i;
}

static size_t word_right(const TextArea *ta, size_t i) {
    while (i < ta->len && !is_word(ta->text[i])) i = next_cp(ta->text, ta->len, i);
    while (i < ta->len && is_word(ta->text[i])) i = next_cp(ta->text, ta->len, i);
    return i;
}

static void copy_selection(struct nk_context *ctx, const TextArea *ta) {
    if (!has_sel(ta) || !ctx->clip.copy) return;
    ctx->clip.copy(ctx->clip.userdata, ta->text + sel_lo(ta), (int)(sel_hi(ta) - sel_lo(ta)));
}

static void paste(struct nk_context *ctx, TextArea *ta) {
    if (!ctx->clip.paste) return;
    /* Nuklear's paste hook writes into an nk_text_edit, so borrow one. */
    struct nk_text_edit te;
    nk_textedit_init_default(&te);
    te.mode = NK_TEXT_EDIT_MODE_INSERT;
    ctx->clip.paste(ctx->clip.userdata, &te);
    const char *s = (const char *)nk_str_get_const(&te.string);
    int n = nk_str_len_char(&te.string);
    if (s && n > 0) {
        char *clean = (char *)malloc((size_t)n + 1);
        size_t k = 0;
        for (int i = 0; i < n; i++) if (s[i] != '\r') clean[k++] = s[i]; /* CRLF from other apps */
        insert(ta, clean, k, EDIT_OTHER);
        free(clean);
    }
    nk_textedit_free(&te);
}

/* Returns true if the text changed. */
static bool handle_keys(struct nk_context *ctx, TextArea *ta, const struct nk_user_font *f, float row_h,
                        float view_h) {
    struct nk_input *in = &ctx->input;
    bool editable = (ta->flags & TEXTAREA_EDITABLE) != 0;
    bool shift = nk_input_is_key_down(in, NK_KEY_SHIFT);
    bool changed = false;

    if (editable && in->keyboard.text_len > 0) {
        insert(ta, in->keyboard.text, (size_t)in->keyboard.text_len, EDIT_TYPE);
        in->keyboard.text_len = 0;
        changed = true;
    }

    if (key_hit(ta, ctx, NK_KEY_LEFT)) {
        size_t to = (has_sel(ta) && !shift) ? sel_lo(ta) : prev_cp(ta->text, ta->cursor);
        move_to(ta, to, shift, false);
    }
    if (key_hit(ta, ctx, NK_KEY_RIGHT)) {
        size_t to = (has_sel(ta) && !shift) ? sel_hi(ta) : next_cp(ta->text, ta->len, ta->cursor);
        move_to(ta, to, shift, false);
    }
    if (key_hit(ta, ctx, NK_KEY_TEXT_WORD_LEFT)) move_to(ta, word_left(ta, ta->cursor), shift, false);
    if (key_hit(ta, ctx, NK_KEY_TEXT_WORD_RIGHT)) move_to(ta, word_right(ta, ta->cursor), shift, false);

    bool up = key_hit(ta, ctx, NK_KEY_UP), down = key_hit(ta, ctx, NK_KEY_DOWN);
    bool page_up = key_hit(ta, ctx, NK_KEY_SCROLL_UP), page_down = key_hit(ta, ctx, NK_KEY_SCROLL_DOWN);
    if (up || down || page_up || page_down) {
        if (!ta->pref_x_valid) {
            ta->pref_x = x_of(ta, f, ta->cursor);
            ta->pref_x_valid = true;
        }
        long r = (long)row_of(ta, ta->cursor);
        long page = (long)(view_h / row_h);
        if (page < 1) page = 1;
        long step = up ? -1 : down ? 1 : page_up ? -page : page;
        long target = r + step;
        size_t to;
        if (target < 0) to = 0;
        else if (target >= (long)ta->row_count) to = ta->len;
        else to = offset_at_x(ta, f, (size_t)target, ta->pref_x);
        move_to(ta, to, shift, true);
    }

    if (nk_input_is_key_pressed(in, NK_KEY_TEXT_START) || nk_input_is_key_pressed(in, NK_KEY_TEXT_LINE_START)) {
        move_to(ta, ta->rows[row_of(ta, ta->cursor)].start, shift, false);
    }
    if (nk_input_is_key_pressed(in, NK_KEY_TEXT_END) || nk_input_is_key_pressed(in, NK_KEY_TEXT_LINE_END)) {
        move_to(ta, ta->rows[row_of(ta, ta->cursor)].end, shift, false);
    }
    if (nk_input_is_key_pressed(in, NK_KEY_TEXT_SELECT_ALL)) {
        ta->anchor = 0;
        move_to(ta, ta->len, true, false);
    }
    if (nk_input_is_key_pressed(in, NK_KEY_COPY)) copy_selection(ctx, ta);

    if (editable) {
        if (nk_input_is_key_pressed(in, NK_KEY_CUT) && has_sel(ta)) {
            copy_selection(ctx, ta);
            delete_range(ta, sel_lo(ta), sel_hi(ta), EDIT_OTHER);
            changed = true;
        }
        if (nk_input_is_key_pressed(in, NK_KEY_PASTE)) {
            paste(ctx, ta);
            changed = true;
        }
        if (key_hit(ta, ctx, NK_KEY_ENTER)) {
            insert(ta, "\n", 1, EDIT_OTHER);
            changed = true;
        }
        if (nk_input_is_key_pressed(in, NK_KEY_TAB)) {
            insert(ta, "\t", 1, EDIT_TYPE);
            changed = true;
        }
        if (key_hit(ta, ctx, NK_KEY_BACKSPACE)) {
            if (has_sel(ta)) delete_range(ta, sel_lo(ta), sel_hi(ta), EDIT_OTHER);
            else delete_range(ta, prev_cp(ta->text, ta->cursor), ta->cursor, EDIT_BACKSPACE);
            changed = true;
        }
        if (key_hit(ta, ctx, NK_KEY_DEL)) {
            if (has_sel(ta)) delete_range(ta, sel_lo(ta), sel_hi(ta), EDIT_OTHER);
            else delete_range(ta, ta->cursor, next_cp(ta->text, ta->len, ta->cursor), EDIT_DELETE);
            changed = true;
        }
        bool undo = nk_input_is_key_pressed(in, NK_KEY_TEXT_UNDO);
        if ((undo && shift) || nk_input_is_key_pressed(in, NK_KEY_TEXT_REDO)) {
            if (restore(ta, ta->redo, &ta->redo_count, &ta->undo, &ta->undo_count, &ta->undo_cap)) changed = true;
        } else if (undo) {
            if (restore(ta, ta->undo, &ta->undo_count, &ta->redo, &ta->redo_count, &ta->redo_cap)) changed = true;
        }
        if (changed) {
            ta->cursor_placed = true;
            ta->scroll_to_cursor = true;
            ta->pref_x_valid = false;
        }
    }
    return changed;
}

/* ---- Drawing ------------------------------------------------------------ */

static struct nk_rect intersect(struct nk_rect a, struct nk_rect b) {
    float x0 = fmaxf(a.x, b.x), y0 = fmaxf(a.y, b.y);
    float x1 = fminf(a.x + a.w, b.x + b.w), y1 = fminf(a.y + a.h, b.y + b.h);
    return nk_rect(x0, y0, fmaxf(0.0f, x1 - x0), fmaxf(0.0f, y1 - y0));
}

static struct nk_color item_color(const struct nk_style_item *it, struct nk_color fallback) {
    return it->type == NK_STYLE_ITEM_COLOR ? it->data.color : fallback;
}

static int digit_count(int n) {
    int d = 1;
    while (n >= 10) { n /= 10; d++; }
    return d;
}

/* nk_tooltip, but one row per '\n'-separated line. */
static void multiline_tooltip(struct nk_context *ctx, const char *tip) {
    const struct nk_user_font *f = ctx->style.font;
    struct nk_vec2 pad = ctx->style.window.padding;
    float widest = 0.0f;
    for (const char *p = tip;;) {
        const char *nl = strchr(p, '\n');
        float w = font_width(f, p, nl ? (size_t)(nl - p) : strlen(p));
        if (w > widest) widest = w;
        if (!nl) break;
        p = nl + 1;
    }
    if (!nk_tooltip_begin(ctx, widest + 4.0f * pad.x)) return;
    nk_layout_row_dynamic(ctx, f->height + 2.0f, 1);
    for (const char *p = tip;;) {
        const char *nl = strchr(p, '\n');
        nk_text(ctx, p, nl ? (int)(nl - p) : (int)strlen(p), NK_TEXT_LEFT);
        if (!nl) break;
        p = nl + 1;
    }
    nk_tooltip_end(ctx);
}

void textarea_draw(struct nk_context *ctx, TextArea *ta, const TextAreaMarker *markers, int marker_count,
                   TextAreaResult *out) {
    TextAreaResult res = { false, false, 0, -1 };
    struct nk_rect bounds;
    enum nk_widget_layout_states state = nk_widget(&bounds, ctx);
    if (state == NK_WIDGET_INVALID) {
        res.focused = ta->focused;
        if (out) *out = res;
        return;
    }
    struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);
    const struct nk_style_edit *st = &ctx->style.edit;
    const struct nk_user_font *f = (ta->flags & TEXTAREA_MONO) ? fonts_mono() : ctx->style.font;
    if (!f) f = ctx->style.font;
    struct nk_input *in = state == NK_WIDGET_ROM ? NULL : &ctx->input;

    float row_h = f->height + 3.0f;
    float gutter_w = 0.0f, marker_w = 0.0f;
    if (ta->flags & TEXTAREA_GUTTER) {
        marker_w = row_h;
        gutter_w = marker_w + (float)digit_count(ta->line_count) * font_width(f, "0", 1) + 10.0f;
    }
    struct nk_rect inner = nk_rect(bounds.x + TA_PAD, bounds.y + TA_PAD, bounds.w - 2 * TA_PAD,
                                   bounds.h - 2 * TA_PAD);
    float text_x = inner.x + gutter_w;
    float text_w = inner.w - gutter_w - TA_SCROLLBAR_W - 4.0f;
    ensure_layout(ta, f, text_w);
    /* The gutter grows with the digit count, which layout just updated. */
    if (ta->flags & TEXTAREA_GUTTER) {
        float g2 = marker_w + (float)digit_count(ta->line_count) * font_width(f, "0", 1) + 10.0f;
        if (g2 != gutter_w) {
            gutter_w = g2;
            text_x = inner.x + gutter_w;
            text_w = inner.w - gutter_w - TA_SCROLLBAR_W - 4.0f;
            ensure_layout(ta, f, text_w);
        }
    }
    float content_h = (float)ta->row_count * row_h;
    float max_scroll = fmaxf(0.0f, content_h - inner.h);
    struct nk_rect old_clip = canvas->clip;
    struct nk_rect visible = intersect(bounds, old_clip);
    struct nk_rect bar = nk_rect(inner.x + inner.w - TA_SCROLLBAR_W, inner.y, TA_SCROLLBAR_W, inner.h);
    float thumb_h = content_h > inner.h ? fmaxf(20.0f, inner.h * inner.h / content_h) : inner.h;

    if (ta->want_focus) {
        set_focus(ta, true);
        ta->want_focus = false;
    }

    /* ---- Mouse ---- */
    if (in) {
        bool hover = nk_input_is_mouse_hovering_rect(in, visible);
        bool pressed = in->mouse.buttons[NK_BUTTON_LEFT].clicked && in->mouse.buttons[NK_BUTTON_LEFT].down;
        bool left_down = in->mouse.buttons[NK_BUTTON_LEFT].down;
        float mx = in->mouse.pos.x, my = in->mouse.pos.y;

        if (pressed && !hover && ta->focused) set_focus(ta, false);

        if (hover && in->mouse.scroll_delta.y != 0.0f && max_scroll > 0.0f) {
            ta->scroll_y -= in->mouse.scroll_delta.y * row_h * 3.0f;
            in->mouse.scroll_delta.y = 0.0f; /* don't also scroll the window */
        }

        if (pressed && hover) {
            if (max_scroll > 0.0f && mx >= bar.x - 2.0f) {
                float thumb_y = inner.y + (inner.h - thumb_h) * (ta->scroll_y / max_scroll);
                if (my >= thumb_y && my <= thumb_y + thumb_h) {
                    ta->dragging_bar = true;
                    ta->bar_grab = my - thumb_y;
                } else {
                    ta->scroll_y += (my < thumb_y ? -1.0f : 1.0f) * inner.h;
                }
            } else if (mx < text_x && (ta->flags & TEXTAREA_GUTTER)) {
                size_t r = (size_t)fmaxf(0.0f, (my - inner.y + ta->scroll_y) / row_h);
                if (r < ta->row_count) {
                    int line = ta->rows[r].line;
                    res.gutter_click_line = line;
                    if (mx < inner.x + marker_w) {
                        for (int i = 0; i < marker_count; i++) {
                            if (markers[i].line == line && markers[i].id >= 0) {
                                res.marker_click_id = markers[i].id;
                                break;
                            }
                        }
                    }
                }
            } else {
                set_focus(ta, true);
                bool dbl = in->mouse.buttons[NK_BUTTON_DOUBLE].clicked && in->mouse.buttons[NK_BUTTON_DOUBLE].down;
                float rel = (my - inner.y + ta->scroll_y) / row_h;
                size_t pos;
                if (rel < 0.0f) pos = 0;
                else if ((size_t)rel >= ta->row_count) pos = ta->len;
                else pos = offset_at_x(ta, f, (size_t)rel, mx - text_x);
                bool shift = nk_input_is_key_down(in, NK_KEY_SHIFT);
                if (dbl && pos < ta->len && is_word(ta->text[pos])) {
                    ta->anchor = pos;
                    while (ta->anchor > 0 && is_word(ta->text[prev_cp(ta->text, ta->anchor)]))
                        ta->anchor = prev_cp(ta->text, ta->anchor);
                    move_to(ta, word_right(ta, pos), true, false);
                    ta->scroll_to_cursor = false;
                } else {
                    move_to(ta, pos, shift, false);
                    ta->scroll_to_cursor = false;
                    ta->dragging_text = true;
                }
            }
        }
        if (!left_down) ta->dragging_text = ta->dragging_bar = false;
        if (ta->dragging_bar && max_scroll > 0.0f && inner.h > thumb_h) {
            ta->scroll_y = (my - ta->bar_grab - inner.y) / (inner.h - thumb_h) * max_scroll;
        }
        if (ta->dragging_text && !pressed) {
            /* Dragging past the edge scrolls. */
            if (my < inner.y) ta->scroll_y -= row_h;
            if (my > inner.y + inner.h) ta->scroll_y += row_h;
            float rel = (my - inner.y + ta->scroll_y) / row_h;
            size_t pos;
            if (rel < 0.0f) pos = 0;
            else if ((size_t)rel >= ta->row_count) pos = ta->len;
            else pos = offset_at_x(ta, f, (size_t)rel, mx - text_x);
            ta->cursor = pos;
        }
    }

    /* ---- Keyboard ---- */
    if (in && ta->focused) {
        if (handle_keys(ctx, ta, f, row_h, inner.h)) res.changed = true;
        ensure_layout(ta, f, text_w);
        content_h = (float)ta->row_count * row_h;
        max_scroll = fmaxf(0.0f, content_h - inner.h);
        thumb_h = content_h > inner.h ? fmaxf(20.0f, inner.h * inner.h / content_h) : inner.h;
    }
    if (ta->scroll_to_cursor && ta->row_count > 0) {
        float cy = (float)row_of(ta, ta->cursor) * row_h;
        if (cy < ta->scroll_y) ta->scroll_y = cy;
        if (cy + row_h > ta->scroll_y + inner.h) ta->scroll_y = cy + row_h - inner.h;
        ta->scroll_to_cursor = false;
    }
    if (ta->scroll_y > max_scroll) ta->scroll_y = max_scroll;
    if (ta->scroll_y < 0.0f) ta->scroll_y = 0.0f;

    /* ---- Paint ---- */
    struct nk_color bg = item_color(ta->focused ? &st->active : &st->normal, nk_rgb(30, 30, 30));
    nk_fill_rect(canvas, bounds, st->rounding, bg);
    if (st->border > 0.0f) nk_stroke_rect(canvas, bounds, st->rounding, st->border, st->border_color);

    nk_push_scissor(canvas, intersect(inner, old_clip));
    struct nk_color fg = st->text_normal;
    struct nk_color sel_bg = st->selected_normal;
    struct nk_color sel_fg = st->selected_text_normal;
    struct nk_color dim = nk_rgba(fg.r, fg.g, fg.b, 110);
    struct nk_color dot_color = theme_nk(g_theme.note_dot);
    struct nk_color ring_color = theme_nk(g_theme.complexity_marker);
    struct nk_color bar_color = theme_nk(g_theme.duplicate_marker);
    const char *hover_tip = NULL;
    int tip_rank = 0; /* 1 dot, 2 bar, 3 ring */
    bool mouse_in_markers = in && nk_input_is_mouse_hovering_rect(in, intersect(nk_rect(inner.x, inner.y, marker_w, inner.h), old_clip));
    size_t lo = sel_lo(ta), hi = sel_hi(ta);
    float space_w = font_width(f, " ", 1);

    size_t first = (size_t)(ta->scroll_y / row_h);
    for (size_t r = first; r < ta->row_count; r++) {
        float y = inner.y + (float)r * row_h - ta->scroll_y;
        if (y > inner.y + inner.h) break;
        const TextAreaRow *row = &ta->rows[r];
        bool line_start = r == 0 || ta->rows[r - 1].line != row->line;

        bool hovered_row = mouse_in_markers && in->mouse.pos.y >= y && in->mouse.pos.y < y + row_h;
        if (ta->flags & TEXTAREA_GUTTER) {
            /* Bars cover wrapped rows too, so a range reads as one stripe. */
            for (int i = 0; i < marker_count; i++) {
                if (markers[i].style != TEXTAREA_MARKER_BAR) continue;
                if (row->line < markers[i].line || row->line > markers[i].end_line) continue;
                nk_fill_rect(canvas, nk_rect(inner.x + 1.0f, y, 3.0f, row_h), 0, bar_color);
                if (hovered_row && markers[i].tip && tip_rank < 2) {
                    hover_tip = markers[i].tip;
                    tip_rank = 2;
                }
            }
        }
        if ((ta->flags & TEXTAREA_GUTTER) && line_start) {
            float cx = inner.x + marker_w * 0.5f, cy = y + row_h * 0.5f;
            for (int pass = 0; pass < 2; pass++) { /* rings first, dots on top */
                int style = pass == 0 ? TEXTAREA_MARKER_RING : TEXTAREA_MARKER_DOT;
                for (int i = 0; i < marker_count; i++) {
                    if (markers[i].line != row->line || markers[i].style != style) continue;
                    if (style == TEXTAREA_MARKER_RING) {
                        float d = row_h * 0.78f;
                        nk_stroke_circle(canvas, nk_rect(cx - d * 0.5f, cy - d * 0.5f, d, d), 1.6f, ring_color);
                    } else {
                        float d = row_h * 0.42f;
                        nk_fill_circle(canvas, nk_rect(cx - d * 0.5f, cy - d * 0.5f, d, d), dot_color);
                    }
                    /* The ring's tip wins, then a bar's: they say why the
                     * line is marked; a note dot's tip is just the note. */
                    int rank = style == TEXTAREA_MARKER_RING ? 3 : 1;
                    if (hovered_row && markers[i].tip && rank > tip_rank) {
                        hover_tip = markers[i].tip;
                        tip_rank = rank;
                    }
                    break;
                }
            }
            char num[16];
            int n = snprintf(num, sizeof(num), "%d", row->line);
            float nw = font_width(f, num, (size_t)n);
            nk_draw_text(canvas, nk_rect(text_x - 8.0f - nw, y, nw + 1.0f, row_h), num, n, f, nk_rgba(0, 0, 0, 0),
                         dim);
        }

        /* Selection: the part of [lo, hi) on this row, plus a sliver for a
         * selected line break. */
        size_t a = lo > row->start ? lo : row->start;
        size_t b = hi < row->end ? hi : row->end;
        bool nl_selected = hi > row->end && lo <= row->end && row->end < ta->len && ta->text[row->end] == '\n';
        if (has_sel(ta) && (a < b || nl_selected)) {
            float x0 = run_width(f, ta->text + row->start, a - row->start);
            float x1 = run_width(f, ta->text + row->start, b - row->start) + (nl_selected ? space_w : 0.0f);
            if (a <= b) nk_fill_rect(canvas, nk_rect(text_x + x0, y, x1 - x0, row_h), 0, sel_bg);
        }
        draw_run(canvas, f, text_x, y + 1.0f, row_h, ta->text + row->start, row->end - row->start, fg);
        if (has_sel(ta) && a < b) {
            float x0 = run_width(f, ta->text + row->start, a - row->start);
            draw_run(canvas, f, text_x + x0, y + 1.0f, row_h, ta->text + a, b - a, sel_fg);
        }

        if (ta->focused && (ta->flags & TEXTAREA_EDITABLE) && row_of(ta, ta->cursor) == r) {
            float cx = run_width(f, ta->text + row->start, ta->cursor - row->start);
            nk_fill_rect(canvas, nk_rect(text_x + cx, y + 1.0f, 1.5f, row_h - 2.0f), 0, st->cursor_normal);
        }
    }

    if (max_scroll > 0.0f) {
        float thumb_y = inner.y + (inner.h - thumb_h) * (ta->scroll_y / max_scroll);
        struct nk_color tc = item_color(&ctx->style.scrollv.cursor_normal, nk_rgb(90, 90, 90));
        nk_fill_rect(canvas, nk_rect(bar.x, thumb_y, bar.w, thumb_h), bar.w * 0.5f, tc);
    }
    nk_push_scissor(canvas, old_clip);
    if (hover_tip) multiline_tooltip(ctx, hover_tip);

    res.focused = ta->focused;
    if (out) *out = res;
}

/* ---- Stateless label ------------------------------------------------------ */

static TextAreaRow *g_label_rows = NULL;
static size_t g_label_row_cap = 0;

float textarea_label_height(struct nk_context *ctx, const char *text, float width) {
    const struct nk_user_font *f = ctx->style.font;
    size_t count = 0;
    layout_text(f, text ? text : "", text ? strlen(text) : 0, width - 2 * TA_PAD, &g_label_rows, &count,
                &g_label_row_cap);
    return (float)count * (f->height + 3.0f) + 2 * TA_PAD;
}

void textarea_label(struct nk_context *ctx, const char *text) {
    struct nk_rect bounds;
    if (nk_widget(&bounds, ctx) == NK_WIDGET_INVALID) return;
    if (!text) text = "";
    struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);
    const struct nk_user_font *f = ctx->style.font;
    float row_h = f->height + 3.0f;
    size_t count = 0;
    layout_text(f, text, strlen(text), bounds.w - 2 * TA_PAD, &g_label_rows, &count, &g_label_row_cap);
    struct nk_rect old_clip = canvas->clip;
    nk_push_scissor(canvas, intersect(bounds, old_clip));
    for (size_t r = 0; r < count; r++) {
        float y = bounds.y + TA_PAD + (float)r * row_h;
        draw_run(canvas, f, bounds.x + TA_PAD, y, row_h, text + g_label_rows[r].start,
                 g_label_rows[r].end - g_label_rows[r].start, ctx->style.text.color);
    }
    nk_push_scissor(canvas, old_clip);
}
