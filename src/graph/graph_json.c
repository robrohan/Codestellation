#include "graph_json.h"
#include "../common/pathutil.h"
#include <json.h>
#include <stddef.h>
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

/* Optional per-node stats (graph.h): written only once computed, and
 * "not applicable" (-1) fields are left out, so readers treat a missing
 * key as unknown. Older graph.json files simply have none. */
static const struct {
    const char *key;
    size_t offset;
} INT_STATS[] = {
    { "lines", offsetof(NodeStats, lines) },
    { "blank_lines", offsetof(NodeStats, blank_lines) },
    { "comment_lines", offsetof(NodeStats, comment_lines) },
    { "max_indent", offsetof(NodeStats, max_indent) },
    { "parse_errors", offsetof(NodeStats, parse_errors) },
    { "complexity", offsetof(NodeStats, complexity) },
    { "functions", offsetof(NodeStats, functions) },
    { "max_function_complexity", offsetof(NodeStats, max_function_complexity) },
    { "fan_in", offsetof(NodeStats, fan_in) },
    { "fan_out", offsetof(NodeStats, fan_out) },
    { "blast_radius", offsetof(NodeStats, blast_radius) },
    { "cycle_id", offsetof(NodeStats, cycle_id) },
    { "cycle_size", offsetof(NodeStats, cycle_size) },
    { "unresolved", offsetof(NodeStats, unresolved) },
    { "git_commits", offsetof(NodeStats, git_commits) },
    { "git_authors", offsetof(NodeStats, git_authors) },
    { "hotspot_rank", offsetof(NodeStats, hotspot_rank) },
};
#define INT_STAT_COUNT (sizeof(INT_STATS) / sizeof(INT_STATS[0]))

static void write_stats(FILE *f, const NodeStats *st) {
    if (!st->has_stats) return;
    for (size_t k = 0; k < INT_STAT_COUNT; k++) {
        int v = *(const int *)((const char *)st + INT_STATS[k].offset);
        if (v >= 0) fprintf(f, ", \"%s\": %d", INT_STATS[k].key, v);
    }
    if (st->git_last_commit > 0) fprintf(f, ", \"git_last_commit\": %lld", st->git_last_commit);
    if (st->hot_count > 0) {
        fprintf(f, ", \"hot_functions\": [");
        for (int k = 0; k < st->hot_count; k++) {
            fprintf(f, "%s{\"line\": %d, \"complexity\": %d, \"name\": ", k ? ", " : "", st->hot[k].line,
                    st->hot[k].complexity);
            write_escaped(f, st->hot[k].name);
            fputc('}', f);
        }
        fputc(']', f);
    }
}

static void read_stats(struct json_object_s *n, NodeStats *st);

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
        write_stats(f, &g->nodes[i].stats);
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

static void read_stats(struct json_object_s *n, NodeStats *st) {
    struct json_value_s *lines_v = obj_get(n, "lines");
    if (!lines_v || !json_value_as_number(lines_v)) return; /* written before stats existed */
    st->has_stats = true;
    for (size_t k = 0; k < INT_STAT_COUNT; k++) {
        struct json_value_s *v = obj_get(n, INT_STATS[k].key);
        struct json_number_s *num = v ? json_value_as_number(v) : NULL;
        if (num) *(int *)((char *)st + INT_STATS[k].offset) = atoi(num->number);
    }
    struct json_value_s *v = obj_get(n, "git_last_commit");
    struct json_number_s *num = v ? json_value_as_number(v) : NULL;
    if (num) st->git_last_commit = atoll(num->number);

    v = obj_get(n, "hot_functions");
    struct json_array_s *hot = v ? json_value_as_array(v) : NULL;
    for (struct json_array_element_s *e = hot ? hot->start : NULL; e && st->hot_count < STATS_HOT_MAX; e = e->next) {
        struct json_object_s *h = json_value_as_object(e->value);
        if (!h) continue;
        struct json_value_s *lv = obj_get(h, "line"), *cv = obj_get(h, "complexity"), *nv = obj_get(h, "name");
        struct json_number_s *ln = lv ? json_value_as_number(lv) : NULL;
        struct json_number_s *cn = cv ? json_value_as_number(cv) : NULL;
        struct json_string_s *ns = nv ? json_value_as_string(nv) : NULL;
        if (!ln || !cn) continue;
        int k = st->hot_count++;
        st->hot[k].line = atoi(ln->number);
        st->hot[k].complexity = atoi(cn->number);
        size_t nlen = ns ? ns->string_size : 0;
        if (nlen >= sizeof(st->hot[k].name)) nlen = sizeof(st->hot[k].name) - 1;
        if (nlen) memcpy(st->hot[k].name, ns->string, nlen);
        st->hot[k].name[nlen] = '\0';
    }
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
            int id = graph_add_node(out, p, l);
            read_stats(n, &out->nodes[id].stats);
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
