#include "canvas_index.h"
#include "project.h"
#include "../common/pathutil.h"
#include <stdlib.h>
#include <string.h>

static char *lower_dup(const char *s) {
    size_t n = strlen(s);
    char *out = (char *)malloc(n + 1);
    for (size_t i = 0; i <= n; i++) {
        char c = s[i];
        out[i] = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
    }
    return out;
}

int canvas_index_find_canvas(const CanvasIndex *idx, const char *path) {
    for (size_t i = 0; i < idx->canvas_count; i++) {
        if (strcmp(idx->canvases[i].path, path) == 0) return (int)i;
    }
    return -1;
}

static int add_canvas(CanvasIndex *idx, size_t *cap, char *path, char *title, int parent) {
    if (idx->canvas_count == *cap) {
        *cap = *cap ? *cap * 2 : 8;
        idx->canvases = (IndexCanvas *)realloc(idx->canvases, *cap * sizeof(IndexCanvas));
    }
    IndexCanvas *c = &idx->canvases[idx->canvas_count];
    c->path = path;
    c->title = title;
    c->parent = parent;
    return (int)idx->canvas_count++;
}

static void add_entry(CanvasIndex *idx, size_t *cap, int canvas, const CanvasNode *n) {
    if (idx->entry_count == *cap) {
        *cap = *cap ? *cap * 2 : 64;
        idx->entries = (IndexEntry *)realloc(idx->entries, *cap * sizeof(IndexEntry));
    }
    IndexEntry *e = &idx->entries[idx->entry_count++];
    char title[128];
    canvas_node_title(n, title, (int)sizeof(title));
    e->canvas = canvas;
    e->node_id = xstrdup(n->id ? n->id : "");
    e->type = n->type;
    e->weak_link = canvas_node_is_weak_link(n);
    e->title = xstrdup(title);
    const char *searchable = n->type == CNODE_TEXT ? n->text
                           : n->type == CNODE_FILE ? n->file
                           : n->type == CNODE_LINK ? n->url
                           : n->label;
    e->text = lower_dup(searchable ? searchable : "");
}

/* Crumb label for a canvas reached through node n, matching what
 * shift+clicking n shows: its title, else the link's file name. */
static char *crumb_title(const CanvasNode *n, const char *link) {
    char buf[64];
    if (n->type == CNODE_TEXT && canvas_text_title(n->text, buf, (int)sizeof(buf)) > 0) return xstrdup(buf);
    const char *base = strrchr(link, '/');
    base = base ? base + 1 : link;
    size_t len = strlen(base);
    if (len > 7 && strcmp(base + len - 7, ".canvas") == 0) len -= 7;
    char *out = (char *)malloc(len + 1);
    memcpy(out, base, len);
    out[len] = '\0';
    return out;
}

void canvas_index_build(CanvasIndex *idx, const char *root_path, const char *root_title) {
    memset(idx, 0, sizeof(*idx));
    size_t canvas_cap = 0, entry_cap = 0;
    char *root = path_normalize(root_path);
    add_canvas(idx, &canvas_cap, root ? root : xstrdup(root_path), xstrdup(root_title ? root_title : "Project"), -1);

    /* canvases[] doubles as the BFS queue. */
    for (size_t q = 0; q < idx->canvas_count; q++) {
        CanvasDoc doc;
        if (!canvas_doc_read(idx->canvases[q].path, &doc)) {
            canvas_doc_free(&doc);
            continue;
        }
        for (size_t i = 0; i < doc.node_count; i++) {
            const CanvasNode *n = &doc.nodes[i];
            add_entry(idx, &entry_cap, (int)q, n);

            char **links;
            size_t count = canvas_node_canvas_links(n, &links);
            for (size_t k = 0; k < count; k++) {
                char *path = project_resolve_link(idx->canvases[q].path, links[k]);
                /* Only canvases that exist -- following a link creates it,
                 * but indexing shouldn't. */
                if (path_exists(path) && canvas_index_find_canvas(idx, path) < 0) {
                    add_canvas(idx, &canvas_cap, path, crumb_title(n, links[k]), (int)q);
                } else {
                    free(path);
                }
                free(links[k]);
            }
            free(links);
        }
        canvas_doc_free(&doc);
    }
}

void canvas_index_free(CanvasIndex *idx) {
    for (size_t i = 0; i < idx->canvas_count; i++) {
        free(idx->canvases[i].path);
        free(idx->canvases[i].title);
    }
    for (size_t i = 0; i < idx->entry_count; i++) {
        free(idx->entries[i].node_id);
        free(idx->entries[i].title);
        free(idx->entries[i].text);
    }
    free(idx->canvases);
    free(idx->entries);
    memset(idx, 0, sizeof(*idx));
}

size_t canvas_index_search(const CanvasIndex *idx, const char *query, size_t *out, size_t max) {
    if (!query || !*query) return 0;
    char *q = lower_dup(query);
    size_t found = 0;
    for (size_t i = 0; i < idx->entry_count && found < max; i++) {
        const IndexEntry *e = &idx->entries[i];
        if (e->weak_link) continue;
        if (strstr(e->text, q)) out[found++] = i;
    }
    free(q);
    return found;
}
