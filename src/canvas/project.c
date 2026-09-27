#include "project.h"
#include "canvas_doc.h"
#include "../common/pathutil.h"
#include <json.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void project_free(Project *p) {
    free(p->path);
    free(p->dir);
    free(p->title);
    free(p->description);
    free(p->root);
    free(p->root_path);
    free(p->created);
    memset(p, 0, sizeof(*p));
}

static bool is_absolute(const char *p) {
    if (p[0] == '/' || p[0] == '\\') return true;
    /* Windows drive letter, "C:\..." or "C:/..." */
    return ((p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= 'a' && p[0] <= 'z')) && p[1] == ':';
}

char *project_resolve_link(const char *canvas_path, const char *target) {
    char *joined;
    if (is_absolute(target)) {
        joined = xstrdup(target);
    } else {
        char *dir = path_dirname(canvas_path);
        joined = path_join(dir, target);
        free(dir);
    }
    char *norm = path_normalize(joined);
    if (norm) {
        free(joined);
        return norm;
    }
    return joined;
}

static struct json_value_s *obj_get(struct json_object_s *obj, const char *key) {
    size_t key_len = strlen(key);
    for (struct json_object_element_s *e = obj->start; e; e = e->next) {
        if (e->name->string_size == key_len && memcmp(e->name->string, key, key_len) == 0) return e->value;
    }
    return NULL;
}

static char *get_str(struct json_object_s *obj, const char *key, const char *fallback) {
    struct json_value_s *v = obj_get(obj, key);
    struct json_string_s *s = v ? json_value_as_string(v) : NULL;
    if (!s) return fallback ? xstrdup(fallback) : NULL;
    char *out = (char *)malloc(s->string_size + 1);
    memcpy(out, s->string, s->string_size);
    out[s->string_size] = '\0';
    return out;
}

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

bool project_open(const char *path, Project *out) {
    memset(out, 0, sizeof(*out));
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
    struct json_object_s *obj = root ? json_value_as_object(root) : NULL;
    /* A project names its root canvas. Requiring that keeps some other
     * JSON file (picked by mistake) from being "opened" -- and getting a
     * root.canvas created next to it. */
    struct json_value_s *root_v = obj ? obj_get(obj, "root") : NULL;
    if (!obj || !root_v || !json_value_as_string(root_v)) {
        free(root);
        free(buf);
        return false;
    }

    char *norm = path_normalize(path);
    out->path = norm ? norm : xstrdup(path);
    out->dir = path_dirname(out->path);
    out->title = get_str(obj, "title", "Untitled");
    out->description = get_str(obj, "description", "");
    out->root = get_str(obj, "root", "root.canvas");
    out->created = get_str(obj, "created", NULL);
    free(root);
    free(buf);

    out->root_path = project_resolve_link(out->path, out->root);
    if (!path_exists(out->root_path) && !canvas_doc_write_empty(out->root_path)) {
        project_free(out);
        return false;
    }
    return true;
}

bool project_create(const char *dir, const char *title, Project *out) {
    memset(out, 0, sizeof(*out));
    char *path = path_join(dir, "project.json");
    if (path_exists(path)) {
        free(path);
        return false;
    }

    char created[32];
    time_t now = time(NULL);
    strftime(created, sizeof(created), "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));

    FILE *f = fopen(path, "w");
    if (!f) {
        free(path);
        return false;
    }
    fputs("{\n  \"title\": ", f);
    write_escaped(f, title && *title ? title : "Untitled");
    fputs(",\n  \"description\": \"\",\n  \"root\": \"root.canvas\",\n  \"created\": ", f);
    write_escaped(f, created);
    fputs("\n}\n", f);
    bool ok = fclose(f) == 0;

    ok = ok && project_open(path, out); /* creates root.canvas */
    free(path);
    return ok;
}
