#include "overlay.h"
#include "filehash.h"
#include "../common/pathutil.h"
#include <json.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void overlay_init(Overlay *ov) {
    ov->entries = NULL;
    ov->count = 0;
    ov->cap = 0;
}

void overlay_free(Overlay *ov) {
    for (size_t i = 0; i < ov->count; i++) free(ov->entries[i].path);
    free(ov->entries);
    ov->entries = NULL;
    ov->count = ov->cap = 0;
}

static OverlayEntry *overlay_find_mut(Overlay *ov, const char *path) {
    for (size_t i = 0; i < ov->count; i++) {
        if (strcmp(ov->entries[i].path, path) == 0) return &ov->entries[i];
    }
    return NULL;
}

const OverlayEntry *overlay_find(const Overlay *ov, const char *path) {
    return overlay_find_mut((Overlay *)ov, path);
}

void overlay_set(Overlay *ov, const char *path, uint64_t hash, float x, float y, float z) {
    OverlayEntry *e = overlay_find_mut(ov, path);
    if (!e) {
        if (ov->count == ov->cap) {
            ov->cap = ov->cap ? ov->cap * 2 : 8;
            ov->entries = (OverlayEntry *)realloc(ov->entries, ov->cap * sizeof(OverlayEntry));
        }
        e = &ov->entries[ov->count++];
        e->path = xstrdup(path);
    }
    e->hash = hash;
    e->x = x;
    e->y = y;
    e->z = z;
}

/* -- JSON I/O, mirrors src/graph/graph_json.c's hand-rolled convention -- */

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

bool overlay_write_json(const Overlay *ov, const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) return false;

    fprintf(f, "{\"version\": 1, \"positions\": [");
    for (size_t i = 0; i < ov->count; i++) {
        if (i > 0) fputc(',', f);
        char hex[17];
        file_hash_to_hex(ov->entries[i].hash, hex);
        fprintf(f, "{\"path\": ");
        write_escaped(f, ov->entries[i].path);
        fprintf(f, ", \"hash\": \"%s\", \"x\": %g, \"y\": %g, \"z\": %g}",
                hex, ov->entries[i].x, ov->entries[i].y, ov->entries[i].z);
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

static float obj_get_float(struct json_object_s *obj, const char *key) {
    struct json_value_s *v = obj_get(obj, key);
    struct json_number_s *n = v ? json_value_as_number(v) : NULL;
    return n ? (float)atof(n->number) : 0.0f;
}

bool overlay_read_json(const char *path, Overlay *out) {
    overlay_init(out);

    FILE *f = fopen(path, "rb");
    if (!f) return true; /* no overlay yet -- not an error */
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0) { fclose(f); return true; }
    char *buf = (char *)malloc((size_t)size);
    size_t got = fread(buf, 1, (size_t)size, f);
    fclose(f);

    struct json_value_s *root = json_parse(buf, got);
    struct json_object_s *root_obj = root ? json_value_as_object(root) : NULL;
    if (!root_obj) {
        free(root);
        free(buf);
        return false; /* file exists but doesn't parse -- a real problem */
    }

    struct json_value_s *positions_v = obj_get(root_obj, "positions");
    struct json_array_s *positions_arr = positions_v ? json_value_as_array(positions_v) : NULL;
    if (positions_arr) {
        for (struct json_array_element_s *e = positions_arr->start; e; e = e->next) {
            struct json_object_s *p = json_value_as_object(e->value);
            if (!p) continue;
            struct json_value_s *path_v = obj_get(p, "path");
            struct json_value_s *hash_v = obj_get(p, "hash");
            struct json_string_s *path_s = path_v ? json_value_as_string(path_v) : NULL;
            struct json_string_s *hash_s = hash_v ? json_value_as_string(hash_v) : NULL;
            if (!path_s) continue;

            char *node_path = json_str_dup(path_s);
            uint64_t hash = 0;
            if (hash_s) {
                char *hex = json_str_dup(hash_s);
                file_hash_from_hex(hex, &hash); /* malformed hex just leaves hash 0 */
                free(hex);
            }
            overlay_set(out, node_path, hash,
                        obj_get_float(p, "x"), obj_get_float(p, "y"), obj_get_float(p, "z"));
            free(node_path);
        }
    }

    free(root);
    free(buf);
    return true;
}
