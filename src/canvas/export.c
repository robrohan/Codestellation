#include "export.h"
#include "canvas_doc.h"
#include "../common/code_cache.h"
#include "../common/pathutil.h"
#include "../graph/graph.h"
#include "../graph/graph_json.h"
#include "../notes/notes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_TRAIL 32
#define TOP_FILES 5

typedef struct {
    FILE *f;
    const Project *project;
    ExportStyle style;
    char **visited;          /* normalized canvas paths already written */
    size_t visited_count, visited_cap;
    const char *trail[MAX_TRAIL];   /* section titles from the root down */
    int trail_len;
} Exporter;

/* ---- small helpers ------------------------------------------------------ */

static bool visited(const Exporter *x, const char *path) {
    for (size_t i = 0; i < x->visited_count; i++) {
        if (strcmp(x->visited[i], path) == 0) return true;
    }
    return false;
}

static void mark_visited(Exporter *x, const char *path) {
    if (x->visited_count == x->visited_cap) {
        x->visited_cap = x->visited_cap ? x->visited_cap * 2 : 16;
        x->visited = (char **)realloc(x->visited, x->visited_cap * sizeof(char *));
    }
    x->visited[x->visited_count++] = xstrdup(path);
}

static void heading(Exporter *x, int level, const char *text) {
    if (level > 6) level = 6;
    for (int i = 0; i < level; i++) fputc('#', x->f);
    fprintf(x->f, " %s\n\n", text);
}

/* "EU-West › api-01" -- the current trail plus one more title. */
static void trail_with(const Exporter *x, const char *last, char *buf, size_t cap) {
    size_t o = 0;
    buf[0] = '\0';
    for (int i = 0; i < x->trail_len && o < cap; i++) o += (size_t)snprintf(buf + o, cap - o, "%s \xE2\x80\xBA ", x->trail[i]);
    if (o < cap) snprintf(buf + o, cap - o, "%s", last);
}

static void node_title(const CanvasNode *n, char *buf, int cap) {
    if (canvas_node_title(n, buf, cap) == 0) snprintf(buf, (size_t)cap, "(untitled)");
}

/* Crumb label for a child canvas reached through n: its title, else the
 * link's file name without ".canvas" (matches the app's breadcrumbs). */
static void child_title(const CanvasNode *n, const char *link, char *buf, size_t cap) {
    if (n->type == CNODE_TEXT && canvas_text_title(n->text, buf, (int)cap) > 0) return;
    const char *base = strrchr(link, '/');
    snprintf(buf, cap, "%s", base ? base + 1 : link);
    size_t len = strlen(buf);
    if (len > 7 && strcmp(buf + len - 7, ".canvas") == 0) buf[len - 7] = '\0';
}

/* Writes a text box's markdown minus its title line, with headings demoted
 * under `level` and [[links]] turned into `code` spans. */
static void write_body(Exporter *x, const char *md, int level) {
    const char *p = md ? md : "";
    /* Skip the first non-blank line: it's the box title, already the heading. */
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    const char *eol = strchr(p, '\n');
    p = eol ? eol + 1 : p + strlen(p);

    bool wrote = false;
    while (*p) {
        eol = strchr(p, '\n');
        size_t n = eol ? (size_t)(eol - p) : strlen(p);
        size_t k = 0;
        bool is_heading = false;
        while (k < n && p[k] == '#') k++;
        if (k > 0 && k < n && p[k] == ' ') {
            is_heading = true;
            int lv = level + (int)k;
            for (int i = 0; i < (lv > 6 ? 6 : lv); i++) fputc('#', x->f);
            p += k;
            n -= k;
        }
        for (size_t i = 0; i < n; i++) {
            if (p[i] == '[' && i + 1 < n && p[i + 1] == '[') {
                const char *end = strstr(p + i + 2, "]]");
                if (end && (size_t)(end - p) < n) {
                    /* Show an alias if one's given, else the target. */
                    const char *s = p + i + 2;
                    const char *bar = memchr(s, '|', (size_t)(end - s));
                    const char *shown = bar ? bar + 1 : s;
                    fputc('`', x->f);
                    fwrite(shown, 1, (size_t)(end - shown), x->f);
                    fputc('`', x->f);
                    i = (size_t)(end - p) + 1;
                    continue;
                }
            }
            if (p[i] != '\r') fputc(p[i], x->f);
        }
        /* Each line of a box is its own line on the canvas; in Markdown
         * consecutive lines would merge into one paragraph, so end
         * non-blank lines with a hard break (two spaces). */
        bool blank = true;
        for (size_t i = 0; i < n && blank; i++) blank = p[i] == ' ' || p[i] == '\t' || p[i] == '\r';
        if (!blank && !is_heading) fputs("  ", x->f);
        fputc('\n', x->f);
        wrote = true;
        p = eol ? eol + 1 : p + n;
    }
    if (wrote) fputc('\n', x->f);
}

/* How a folder is shown: relative when it's inside the project, otherwise
 * absolute with the home directory as "~" (a long ../../.. chain back to
 * the filesystem root helps nobody). Caller frees. */
static char *display_path(const char *project_dir, const char *abs) {
    size_t plen = strlen(project_dir);
    if (strncmp(abs, project_dir, plen) == 0 && (abs[plen] == '/' || abs[plen] == '\\')) {
        return path_relative(project_dir, abs);
    }
    const char *home = getenv("HOME");
    size_t hlen = home ? strlen(home) : 0;
    if (hlen > 1 && strncmp(abs, home, hlen) == 0 && (abs[hlen] == '/' || abs[hlen] == '\\')) {
        char *out = (char *)malloc(strlen(abs) - hlen + 2);
        out[0] = '~';
        strcpy(out + 1, abs + hlen);
        return out;
    }
    return xstrdup(abs);
}

/* ---- code summaries ----------------------------------------------------- */

/* Path shown for a file inside one of the box's folders: that folder's
 * name plus the path under it. */
static const char *short_path(const char *path, char *const *dirs, size_t count, char *buf, size_t cap) {
    for (size_t i = 0; i < count; i++) {
        size_t len = strlen(dirs[i]);
        if (strncmp(path, dirs[i], len) == 0 && (path[len] == '/' || path[len] == '\\')) {
            const char *base = strrchr(dirs[i], '/');
            snprintf(buf, cap, "%s/%s", base ? base + 1 : dirs[i], path + len + 1);
            return buf;
        }
    }
    return path;
}

static void write_code_summary(Exporter *x, char *const *raw_dirs, size_t count) {
    char **dirs = code_cache_normalize_dirs((const char *const *)raw_dirs, count);

    fputs("**Code:** ", x->f);
    for (size_t i = 0; i < count; i++) {
        char *rel = display_path(x->project->dir, dirs[i]);
        fprintf(x->f, "%s`%s`", i ? ", " : "", rel);
        free(rel);
    }
    fputs("\n\n", x->f);

    char *cache = code_cache_dir(dirs, count);
    char *graph_path = cache ? path_join(cache, "graph.json") : NULL;
    Graph g;
    if (!graph_path || !graph_read_json(graph_path, &g)) {
        fputs("_Not built yet -- open this box's code in Codestellation once to include a summary._\n\n", x->f);
        free(graph_path);
        free(cache);
        code_cache_free_dirs(dirs, count);
        return;
    }

    /* Languages, by file count. */
    const char *langs[32];
    int lang_counts[32];
    int nlangs = 0;
    for (size_t i = 0; i < g.node_count; i++) {
        const char *l = g.nodes[i].language ? g.nodes[i].language : "?";
        int k = 0;
        while (k < nlangs && strcmp(langs[k], l) != 0) k++;
        if (k == nlangs && nlangs < 32) {
            langs[nlangs] = l;
            lang_counts[nlangs++] = 0;
        }
        if (k < nlangs) lang_counts[k]++;
    }
    fprintf(x->f, "- %zu source files, %zu dependencies between them\n", g.node_count, g.edge_count);
    fputs("- Languages: ", x->f);
    for (int k = 0; k < nlangs; k++) fprintf(x->f, "%s%s (%d)", k ? ", " : "", langs[k], lang_counts[k]);
    fputc('\n', x->f);

    /* Most depended-on files: highest in-degree. Edge endpoints are node
     * ids, which equal node indices in a graph.json this app wrote. */
    int *indeg = (int *)calloc(g.node_count ? g.node_count : 1, sizeof(int));
    for (size_t e = 0; e < g.edge_count; e++) {
        int t = g.edges[e].target;
        if (t >= 0 && (size_t)t < g.node_count) indeg[t]++;
    }
    fputs("- Most depended-on:", x->f);
    bool any = false;
    for (int r = 0; r < TOP_FILES; r++) {
        int best = -1;
        for (size_t i = 0; i < g.node_count; i++) {
            if (indeg[i] > 0 && (best < 0 || indeg[i] > indeg[best])) best = (int)i;
        }
        if (best < 0) break;
        char buf[1024];
        fprintf(x->f, "%s `%s` (%d)", any ? "," : "", short_path(g.nodes[best].path, dirs, count, buf, sizeof(buf)),
                indeg[best]);
        indeg[best] = -1;
        any = true;
    }
    fputs(any ? "\n\n" : " none\n\n", x->f);
    free(indeg);

    /* Notes made in the 3D view on these folders. */
    char *notes_path = path_join(cache, "graph.notes.md");
    NoteSet notes;
    notes_read_md(notes_path, &notes);
    if (notes.count > 0) {
        fputs(x->style == EXPORT_LLM ? "Notes on the code:\n\n" : "**Notes on the code:**\n\n", x->f);
        for (size_t i = 0; i < notes.count; i++) {
            const Note *n = &notes.notes[i];
            char buf[1024];
            if (n->path) {
                const char *sp = short_path(n->path, dirs, count, buf, sizeof(buf));
                if (n->has_line) fprintf(x->f, "- `%s:%d`", sp, n->line);
                else fprintf(x->f, "- `%s`", sp);
            } else {
                fputs("- ", x->f);
                for (size_t k = 0; k < n->group_path_count; k++) {
                    fprintf(x->f, "%s`%s`", k ? ", " : "", short_path(n->group_paths[k], dirs, count, buf, sizeof(buf)));
                }
            }
            /* Keep multi-line bodies inside the list item. */
            fputs(": ", x->f);
            for (const char *b = n->body ? n->body : ""; *b; b++) {
                if (*b == '\n') {
                    if (b[1]) fputs("\n  ", x->f);
                } else {
                    fputc(*b, x->f);
                }
            }
            fputc('\n', x->f);
        }
        fputc('\n', x->f);
    }
    notes_free(&notes);
    free(notes_path);
    graph_free(&g);
    free(graph_path);
    free(cache);
    code_cache_free_dirs(dirs, count);
}

/* Folder links on a box: [[targets]] (or a file node's path) that resolve,
 * relative to the canvas, to directories. Returns count; *out malloc'd. */
static size_t dir_links(const CanvasNode *n, const char *canvas_path, char ***out) {
    *out = NULL;
    size_t count = 0, cap = 0;
    if (canvas_node_is_weak_link(n)) return 0;
    const char *single = n->type == CNODE_FILE ? n->file : NULL;
    if (!single && (n->type != CNODE_TEXT || !n->text)) return 0;
    const char *p = n->text;
    for (;;) {
        const char *t;
        size_t len;
        if (single) {
            t = single;
            len = strlen(single);
        } else {
            p = strstr(p, "[[");
            if (!p) break;
            p += 2;
            const char *end = strstr(p, "]]");
            if (!end) break;
            len = 0;
            while (p + len < end && p[len] != '|' && p[len] != '#') len++;
            t = p;
            p = end + 2;
        }
        char *raw = (char *)malloc(len + 1);
        memcpy(raw, t, len);
        raw[len] = '\0';
        char *path = project_resolve_link(canvas_path, raw);
        free(raw);
        if (path_is_dir(path)) {
            if (count == cap) {
                cap = cap ? cap * 2 : 4;
                *out = (char **)realloc(*out, cap * sizeof(char *));
            }
            (*out)[count++] = path;
        } else {
            free(path);
        }
        if (single) break;
    }
    return count;
}

/* ---- canvases ----------------------------------------------------------- */

static int cmp_reading_order(const void *a, const void *b) {
    const CanvasNode *na = *(const CanvasNode *const *)a, *nb = *(const CanvasNode *const *)b;
    /* Rows first (within a tolerance, so slightly misaligned boxes still
     * read left to right), then columns. */
    if (na->y + 40.0f < nb->y) return -1;
    if (nb->y + 40.0f < na->y) return 1;
    return (na->x > nb->x) - (na->x < nb->x);
}

/* The smallest group whose rect contains n's centre, or -1. */
static int containing_group(const CanvasDoc *doc, const CanvasNode *n) {
    float cx = n->x + n->w * 0.5f, cy = n->y + n->h * 0.5f;
    int best = -1;
    float best_area = 0.0f;
    for (size_t i = 0; i < doc->node_count; i++) {
        const CanvasNode *g = &doc->nodes[i];
        if (g->type != CNODE_GROUP || g == n) continue;
        if (cx < g->x || cx > g->x + g->w || cy < g->y || cy > g->y + g->h) continue;
        float area = g->w * g->h;
        if (best < 0 || area < best_area) {
            best = (int)i;
            best_area = area;
        }
    }
    return best;
}

/* Title of a weak link's target, with the canvas it lives on. */
static void weak_link_description(const CanvasNode *n, const char *canvas_path, char *buf, size_t cap) {
    char *path = project_resolve_link(canvas_path, n->file);
    CanvasDoc doc;
    char title[128] = "(missing)";
    if (canvas_doc_read(path, &doc)) {
        int i = canvas_doc_find_node(&doc, n->subpath + 1);
        if (i >= 0) node_title(&doc.nodes[i], title, (int)sizeof(title));
    }
    canvas_doc_free(&doc);
    free(path);
    char where[128];
    child_title(n, n->file, where, sizeof(where));
    snprintf(buf, cap, "%s (on %s)", title, where);
}

static void export_canvas(Exporter *x, const char *path, const char *title, int depth);

static void write_box(Exporter *x, const CanvasDoc *doc, const CanvasNode *n, const char *canvas_path, int level) {
    (void)doc;
    char title[128], heading_text[512];
    if (canvas_node_is_weak_link(n)) {
        char desc[300];
        weak_link_description(n, canvas_path, desc, sizeof(desc));
        fprintf(x->f, "- Reference to **%s**\n\n", desc);
        return;
    }
    node_title(n, title, (int)sizeof(title));
    if (x->style == EXPORT_LLM) trail_with(x, title, heading_text, sizeof(heading_text));
    else snprintf(heading_text, sizeof(heading_text), "%s", title);
    heading(x, level, heading_text);

    switch (n->type) {
        case CNODE_TEXT: write_body(x, n->text, level); break;
        case CNODE_FILE: fprintf(x->f, "File: `%s`\n\n", n->file ? n->file : ""); break;
        case CNODE_LINK: fprintf(x->f, "Link: <%s>\n\n", n->url ? n->url : ""); break;
        case CNODE_GROUP: break;
    }

    char **dirs;
    size_t ndirs = dir_links(n, canvas_path, &dirs);
    if (ndirs) write_code_summary(x, dirs, ndirs);
    for (size_t i = 0; i < ndirs; i++) free(dirs[i]);
    free(dirs);

    char **links;
    size_t nlinks = canvas_node_canvas_links(n, &links);
    for (size_t i = 0; i < nlinks; i++) {
        char ct[128];
        child_title(n, links[i], ct, sizeof(ct));
        fprintf(x->f, "_Detailed in section \xE2\x80\x9C%s\xE2\x80\x9D._\n\n", ct);
        free(links[i]);
    }
    free(links);
}

static void write_connections(Exporter *x, const CanvasDoc *doc, const char *canvas_path, int level) {
    if (doc->edge_count == 0) return;
    heading(x, level, "Connections");
    for (size_t e = 0; e < doc->edge_count; e++) {
        const CanvasEdge *ed = &doc->edges[e];
        int a = canvas_doc_find_node(doc, ed->from_node), b = canvas_doc_find_node(doc, ed->to_node);
        if (a < 0 || b < 0) continue;
        char ta[300], tb[300];
        if (canvas_node_is_weak_link(&doc->nodes[a])) weak_link_description(&doc->nodes[a], canvas_path, ta, sizeof(ta));
        else node_title(&doc->nodes[a], ta, (int)sizeof(ta));
        if (canvas_node_is_weak_link(&doc->nodes[b])) weak_link_description(&doc->nodes[b], canvas_path, tb, sizeof(tb));
        else node_title(&doc->nodes[b], tb, (int)sizeof(tb));
        /* Spec defaults: no arrow at the start, one at the end. */
        bool to_arrow = !ed->to_end || strcmp(ed->to_end, "none") != 0;
        bool from_arrow = ed->from_end && strcmp(ed->from_end, "arrow") == 0;
        const char *arrow = (to_arrow && from_arrow) ? "\xE2\x86\x94" : to_arrow ? "\xE2\x86\x92"
                          : from_arrow ? "\xE2\x86\x90" : "\xE2\x80\x94";
        fprintf(x->f, "- %s %s %s", ta, arrow, tb);
        if (ed->label && *ed->label) fprintf(x->f, ": %s", ed->label);
        fputc('\n', x->f);
    }
    fputc('\n', x->f);
}

static void export_canvas(Exporter *x, const char *path, const char *title, int depth) {
    mark_visited(x, path);
    CanvasDoc doc;
    if (!canvas_doc_read(path, &doc)) {
        canvas_doc_free(&doc);
        return;
    }
    int level = 2 + depth;
    char section[512];
    if (x->style == EXPORT_LLM && x->trail_len > 0) trail_with(x, title, section, sizeof(section));
    else snprintf(section, sizeof(section), "%s", title);
    heading(x, level, section);
    if (x->trail_len < MAX_TRAIL) x->trail[x->trail_len++] = title;

    /* Reading order: groups (each with the boxes inside it), then loose boxes. */
    const CanvasNode **order = (const CanvasNode **)malloc((doc.node_count ? doc.node_count : 1) * sizeof(*order));
    for (size_t i = 0; i < doc.node_count; i++) order[i] = &doc.nodes[i];
    qsort(order, doc.node_count, sizeof(*order), cmp_reading_order);

    for (size_t gi = 0; gi < doc.node_count; gi++) {
        const CanvasNode *g = order[gi];
        if (g->type != CNODE_GROUP) continue;
        char gt[160];
        node_title(g, gt, (int)sizeof(gt));
        char gh[200];
        snprintf(gh, sizeof(gh), "%s (group)", gt);
        heading(x, level + 1, gh);
        for (size_t i = 0; i < doc.node_count; i++) {
            const CanvasNode *n = order[i];
            if (n->type == CNODE_GROUP) continue;
            int c = containing_group(&doc, n);
            if (c >= 0 && &doc.nodes[c] == g) write_box(x, &doc, n, path, level + 2);
        }
    }
    for (size_t i = 0; i < doc.node_count; i++) {
        const CanvasNode *n = order[i];
        if (n->type != CNODE_GROUP && containing_group(&doc, n) < 0) write_box(x, &doc, n, path, level + 1);
    }
    write_connections(x, &doc, path, level + 1);

    /* Child canvases, depth-first, in reading order. */
    for (size_t i = 0; i < doc.node_count; i++) {
        const CanvasNode *n = order[i];
        char **links;
        size_t nlinks = canvas_node_canvas_links(n, &links);
        for (size_t k = 0; k < nlinks; k++) {
            char *child = project_resolve_link(path, links[k]);
            if (path_exists(child) && !visited(x, child)) {
                char ct[128];
                child_title(n, links[k], ct, sizeof(ct));
                export_canvas(x, child, ct, depth + 1);
            }
            free(child);
            free(links[k]);
        }
        free(links);
    }

    x->trail_len--;
    free(order);
    canvas_doc_free(&doc);
}

bool export_project(const Project *p, ExportStyle style, const char *out_path) {
    FILE *f = fopen(out_path, "w");
    if (!f) return false;
    Exporter x = { 0 };
    x.f = f;
    x.project = p;
    x.style = style;

    if (style == EXPORT_LLM) {
        fprintf(f, "# System brief: %s\n\n", p->title);
        fputs("> Context for an AI assistant. This describes a software system as nested diagrams. Each section "
              "is one diagram, and each subsection is a component on it; a component's heading gives its full "
              "path from the top level. \"Connections\" lists how components talk to each other. \"Code\" "
              "summarises the source folders behind a component, with notes the author made on specific files "
              "and lines. A \"Reference\" is a component that lives in another section, shown here because "
              "something connects to it.\n\n", f);
        if (p->description && *p->description) fprintf(f, "%s\n\n", p->description);
    } else {
        char date[32];
        time_t now = time(NULL);
        strftime(date, sizeof(date), "%Y-%m-%d", localtime(&now));
        fprintf(f, "# %s\n\n", p->title);
        if (p->description && *p->description) fprintf(f, "%s\n\n", p->description);
        fprintf(f, "_Generated by Codestellation on %s._\n\n", date);
    }

    export_canvas(&x, p->root_path, style == EXPORT_LLM ? "Top level" : "Overview", 0);

    for (size_t i = 0; i < x.visited_count; i++) free(x.visited[i]);
    free(x.visited);
    return fclose(f) == 0;
}
