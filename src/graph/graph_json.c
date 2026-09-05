#include "graph_json.h"
#include "../common/pathutil.h"
#include <json.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

bool graph_write_json(const Graph *g, const char *out_path) {
    FILE *f = fopen(out_path, "w");
    if (!f) return false;

    fprintf(f, "{\"directed\": true, \"graph\": {}, \"nodes\": [");
    for (size_t i = 0; i < g->node_count; i++) {
        if (i > 0) fputc(',', f);
        fprintf(f, "{\"id\": %d, \"path\": ", g->nodes[i].id);
        write_escaped(f, g->nodes[i].path);
        fprintf(f, ", \"language\": ");
        write_escaped(f, g->nodes[i].language);
        fputc('}', f);
    }
    fprintf(f, "], \"links\": [");
    for (size_t i = 0; i < g->edge_count; i++) {
        if (i > 0) fputc(',', f);
        fprintf(f, "{\"source\": %d, \"target\": %d, \"kind\": ",
                g->edges[i].source, g->edges[i].target);
        write_escaped(f, g->edges[i].kind);
        fputc('}', f);
    }
    fprintf(f, "]}\n");

    fclose(f);
    return true;
}

static char *json_str_dup(const struct json_string_s *s) {
    if (!s) return xstrdup("");
    char *out = (char *)malloc(s->string_size + 1);
    memcpy(out, s->string, s->string_size);
    out[s->string_size] = '\0';
    return out;
}

static struct json_value_s *obj_get(struct json_object_s *obj, const char *key) {
    size_t key_len = strlen(key);
    for (struct json_object_element_s *e = obj->start; e; e = e->next) {
        if (e->name->string_size == key_len && memcmp(e->name->string, key, key_len) == 0) {
            return e->value;
        }
    }
    return NULL;
}

bool graph_read_json(const char *path, Graph *out) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0) { fclose(f); return false; }
    char *buf = (char *)malloc((size_t)size);
    size_t got = fread(buf, 1, (size_t)size, f);
    fclose(f);

    struct json_value_s *root = json_parse(buf, got);
    struct json_object_s *root_obj = root ? json_value_as_object(root) : NULL;
    if (!root_obj) {
        free(root);
        free(buf);
        return false;
    }

    graph_init(out);

    struct json_value_s *nodes_v = obj_get(root_obj, "nodes");
    struct json_array_s *nodes_arr = nodes_v ? json_value_as_array(nodes_v) : NULL;
    if (nodes_arr) {
        for (struct json_array_element_s *e = nodes_arr->start; e; e = e->next) {
            struct json_object_s *n = json_value_as_object(e->value);
            if (!n) continue;
            struct json_value_s *path_v = obj_get(n, "path");
            struct json_value_s *lang_v = obj_get(n, "language");
            char *p = json_str_dup(path_v ? json_value_as_string(path_v) : NULL);
            char *l = json_str_dup(lang_v ? json_value_as_string(lang_v) : NULL);
            graph_add_node(out, p, l);
            free(p);
            free(l);
        }
    }

    struct json_value_s *links_v = obj_get(root_obj, "links");
    struct json_array_s *links_arr = links_v ? json_value_as_array(links_v) : NULL;
    if (links_arr) {
        for (struct json_array_element_s *e = links_arr->start; e; e = e->next) {
            struct json_object_s *l = json_value_as_object(e->value);
            if (!l) continue;
            struct json_value_s *src_v = obj_get(l, "source");
            struct json_value_s *tgt_v = obj_get(l, "target");
            struct json_value_s *kind_v = obj_get(l, "kind");
            struct json_number_s *src_n = src_v ? json_value_as_number(src_v) : NULL;
            struct json_number_s *tgt_n = tgt_v ? json_value_as_number(tgt_v) : NULL;
            if (!src_n || !tgt_n) continue;
            char *k = json_str_dup(kind_v ? json_value_as_string(kind_v) : NULL);
            graph_add_edge(out, atoi(src_n->number), atoi(tgt_n->number), k);
            free(k);
        }
    }

    free(root);
    free(buf);
    return true;
}
