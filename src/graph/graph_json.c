#include "graph_json.h"
#include <stdio.h>

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
