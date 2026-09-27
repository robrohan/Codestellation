#include "canvas_doc.h"
#include "../common/pathutil.h"
#include <json.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void canvas_doc_init(CanvasDoc *doc) {
    memset(doc, 0, sizeof(*doc));
}

static void node_free(CanvasNode *n) {
    free(n->id);
    free(n->color);
    free(n->text);
    free(n->file);
    free(n->subpath);
    free(n->url);
    free(n->label);
}

static void edge_free(CanvasEdge *e) {
    free(e->id);
    free(e->from_node);
    free(e->to_node);
    free(e->from_side);
    free(e->to_side);
    free(e->from_end);
    free(e->to_end);
    free(e->color);
    free(e->label);
}

void canvas_doc_free(CanvasDoc *doc) {
    for (size_t i = 0; i < doc->node_count; i++) node_free(&doc->nodes[i]);
    for (size_t i = 0; i < doc->edge_count; i++) edge_free(&doc->edges[i]);
    free(doc->nodes);
    free(doc->edges);
    canvas_doc_init(doc);
}

/* 16 hex digits, the same shape Obsidian uses. Not cryptographic -- ids
 * only need to be unique within one canvas. */
static char *new_id(void) {
    static uint64_t state = 0;
    if (state == 0) state = (uint64_t)time(NULL) ^ ((uint64_t)(uintptr_t)&state << 16) ^ 0x9E3779B97F4A7C15ULL;
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    char buf[17];
    snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)state);
    return xstrdup(buf);
}

CanvasNode *canvas_doc_add_node(CanvasDoc *doc, CanvasNodeType type) {
    if (doc->node_count == doc->node_cap) {
        doc->node_cap = doc->node_cap ? doc->node_cap * 2 : 16;
        doc->nodes = (CanvasNode *)realloc(doc->nodes, doc->node_cap * sizeof(CanvasNode));
    }
    CanvasNode *n = &doc->nodes[doc->node_count++];
    memset(n, 0, sizeof(*n));
    n->id = new_id();
    n->type = type;
    return n;
}

CanvasEdge *canvas_doc_add_edge(CanvasDoc *doc) {
    if (doc->edge_count == doc->edge_cap) {
        doc->edge_cap = doc->edge_cap ? doc->edge_cap * 2 : 16;
        doc->edges = (CanvasEdge *)realloc(doc->edges, doc->edge_cap * sizeof(CanvasEdge));
    }
    CanvasEdge *e = &doc->edges[doc->edge_count++];
    memset(e, 0, sizeof(*e));
    e->id = new_id();
    return e;
}

void canvas_doc_remove_edge(CanvasDoc *doc, size_t index) {
    if (index >= doc->edge_count) return;
    edge_free(&doc->edges[index]);
    memmove(&doc->edges[index], &doc->edges[index + 1], (doc->edge_count - index - 1) * sizeof(CanvasEdge));
    doc->edge_count--;
}

void canvas_doc_remove_node(CanvasDoc *doc, size_t index) {
    if (index >= doc->node_count) return;
    const char *id = doc->nodes[index].id;
    for (size_t e = doc->edge_count; e-- > 0;) {
        const CanvasEdge *ed = &doc->edges[e];
        if ((ed->from_node && strcmp(ed->from_node, id) == 0) || (ed->to_node && strcmp(ed->to_node, id) == 0)) {
            canvas_doc_remove_edge(doc, e);
        }
    }
    node_free(&doc->nodes[index]);
    memmove(&doc->nodes[index], &doc->nodes[index + 1], (doc->node_count - index - 1) * sizeof(CanvasNode));
    doc->node_count--;
}

int canvas_doc_find_node(const CanvasDoc *doc, const char *id) {
    if (!id) return -1;
    for (size_t i = 0; i < doc->node_count; i++) {
        if (doc->nodes[i].id && strcmp(doc->nodes[i].id, id) == 0) return (int)i;
    }
    return -1;
}

/* ---- reading ------------------------------------------------------------ */

static struct json_value_s *obj_get(struct json_object_s *obj, const char *key) {
    size_t key_len = strlen(key);
    for (struct json_object_element_s *e = obj->start; e; e = e->next) {
        if (e->name->string_size == key_len && memcmp(e->name->string, key, key_len) == 0) return e->value;
    }
    return NULL;
}

/* NULL when absent or not a string. */
static char *get_str(struct json_object_s *obj, const char *key) {
    struct json_value_s *v = obj_get(obj, key);
    struct json_string_s *s = v ? json_value_as_string(v) : NULL;
    if (!s) return NULL;
    char *out = (char *)malloc(s->string_size + 1);
    memcpy(out, s->string, s->string_size);
    out[s->string_size] = '\0';
    return out;
}

static float get_num(struct json_object_s *obj, const char *key, float fallback) {
    struct json_value_s *v = obj_get(obj, key);
    struct json_number_s *n = v ? json_value_as_number(v) : NULL;
    if (!n) return fallback;
    char buf[64];
    size_t len = n->number_size < sizeof(buf) - 1 ? n->number_size : sizeof(buf) - 1;
    memcpy(buf, n->number, len);
    buf[len] = '\0';
    return (float)strtod(buf, NULL);
}

bool canvas_doc_read(const char *path, CanvasDoc *out) {
    canvas_doc_init(out);
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0) { fclose(f); return false; }
    char *buf = (char *)malloc((size_t)size + 1);
    size_t got = fread(buf, 1, (size_t)size, f);
    fclose(f);

    struct json_value_s *root = json_parse(buf, got);
    struct json_object_s *root_obj = root ? json_value_as_object(root) : NULL;
    if (!root_obj) {
        free(root);
        free(buf);
        return false;
    }

    struct json_value_s *nodes_v = obj_get(root_obj, "nodes");
    struct json_array_s *nodes = nodes_v ? json_value_as_array(nodes_v) : NULL;
    for (struct json_array_element_s *e = nodes ? nodes->start : NULL; e; e = e->next) {
        struct json_object_s *o = json_value_as_object(e->value);
        if (!o) continue;
        char *type = get_str(o, "type");
        CanvasNodeType t = CNODE_TEXT;
        if (type && strcmp(type, "file") == 0) t = CNODE_FILE;
        else if (type && strcmp(type, "link") == 0) t = CNODE_LINK;
        else if (type && strcmp(type, "group") == 0) t = CNODE_GROUP;
        free(type);

        CanvasNode *n = canvas_doc_add_node(out, t);
        char *id = get_str(o, "id");
        if (id) {
            free(n->id);
            n->id = id;
        }
        n->x = get_num(o, "x", 0);
        n->y = get_num(o, "y", 0);
        n->w = get_num(o, "width", 250);
        n->h = get_num(o, "height", 60);
        n->color = get_str(o, "color");
        n->text = get_str(o, "text");
        n->file = get_str(o, "file");
        n->subpath = get_str(o, "subpath");
        n->url = get_str(o, "url");
        n->label = get_str(o, "label");
        if (t == CNODE_TEXT && !n->text) n->text = xstrdup("");
    }

    struct json_value_s *edges_v = obj_get(root_obj, "edges");
    struct json_array_s *edges = edges_v ? json_value_as_array(edges_v) : NULL;
    for (struct json_array_element_s *e = edges ? edges->start : NULL; e; e = e->next) {
        struct json_object_s *o = json_value_as_object(e->value);
        if (!o) continue;
        CanvasEdge *ed = canvas_doc_add_edge(out);
        char *id = get_str(o, "id");
        if (id) {
            free(ed->id);
            ed->id = id;
        }
        ed->from_node = get_str(o, "fromNode");
        ed->to_node = get_str(o, "toNode");
        ed->from_side = get_str(o, "fromSide");
        ed->to_side = get_str(o, "toSide");
        ed->from_end = get_str(o, "fromEnd");
        ed->to_end = get_str(o, "toEnd");
        ed->color = get_str(o, "color");
        ed->label = get_str(o, "label");
    }

    free(root);
    free(buf);
    return true;
}

/* ---- writing ------------------------------------------------------------ */

static void write_escaped(FILE *f, const char *s) {
    fputc('"', f);
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        switch (*p) {
            case '"':  fputs("\\\"", f); break;
            case '\\': fputs("\\\\", f); break;
            case '\n': fputs("\\n", f); break;
            case '\r': fputs("\\r", f); break;
            case '\t': fputs("\\t", f); break;
            default:
                if (*p < 0x20) fprintf(f, "\\u%04x", *p);
                else fputc(*p, f);
        }
    }
    fputc('"', f);
}

static void write_opt(FILE *f, const char *key, const char *value) {
    if (!value) return;
    fprintf(f, ",\"%s\":", key);
    write_escaped(f, value);
}

static const char *type_name(CanvasNodeType t) {
    switch (t) {
        case CNODE_FILE: return "file";
        case CNODE_LINK: return "link";
        case CNODE_GROUP: return "group";
        default: return "text";
    }
}

bool canvas_doc_write(const CanvasDoc *doc, const char *path) {
    /* Write to a sibling temp file and rename over the original, so a
     * crash mid-write can't leave a truncated canvas behind. */
    size_t plen = strlen(path);
    char *tmp = (char *)malloc(plen + 5);
    memcpy(tmp, path, plen);
    memcpy(tmp + plen, ".tmp", 5);

    FILE *f = fopen(tmp, "w");
    if (!f) {
        free(tmp);
        return false;
    }

    fputs("{\n\t\"nodes\":[", f);
    for (size_t i = 0; i < doc->node_count; i++) {
        const CanvasNode *n = &doc->nodes[i];
        fputs(i ? ",\n\t\t{" : "\n\t\t{", f);
        fputs("\"id\":", f);
        write_escaped(f, n->id);
        fprintf(f, ",\"type\":\"%s\"", type_name(n->type));
        switch (n->type) {
            case CNODE_TEXT: write_opt(f, "text", n->text ? n->text : ""); break;
            case CNODE_FILE:
                write_opt(f, "file", n->file ? n->file : "");
                write_opt(f, "subpath", n->subpath);
                break;
            case CNODE_LINK: write_opt(f, "url", n->url ? n->url : ""); break;
            case CNODE_GROUP: write_opt(f, "label", n->label); break;
        }
        fprintf(f, ",\"x\":%ld,\"y\":%ld,\"width\":%ld,\"height\":%ld", lroundf(n->x), lroundf(n->y),
                lroundf(n->w), lroundf(n->h));
        write_opt(f, "color", n->color);
        fputc('}', f);
    }
    fputs(doc->node_count ? "\n\t],\n\t\"edges\":[" : "],\n\t\"edges\":[", f);
    for (size_t i = 0; i < doc->edge_count; i++) {
        const CanvasEdge *e = &doc->edges[i];
        fputs(i ? ",\n\t\t{" : "\n\t\t{", f);
        fputs("\"id\":", f);
        write_escaped(f, e->id);
        write_opt(f, "fromNode", e->from_node ? e->from_node : "");
        write_opt(f, "fromSide", e->from_side);
        write_opt(f, "fromEnd", e->from_end);
        write_opt(f, "toNode", e->to_node ? e->to_node : "");
        write_opt(f, "toSide", e->to_side);
        write_opt(f, "toEnd", e->to_end);
        write_opt(f, "color", e->color);
        write_opt(f, "label", e->label);
        fputc('}', f);
    }
    fputs(doc->edge_count ? "\n\t]\n}\n" : "]\n}\n", f);

    bool ok = fclose(f) == 0;
    if (ok) {
#if defined(_WIN32)
        remove(path); /* Windows rename() won't replace an existing file */
#endif
        ok = rename(tmp, path) == 0;
    }
    if (!ok) remove(tmp);
    free(tmp);
    return ok;
}

bool canvas_doc_write_empty(const char *path) {
    CanvasDoc empty;
    canvas_doc_init(&empty);
    return canvas_doc_write(&empty, path);
}
