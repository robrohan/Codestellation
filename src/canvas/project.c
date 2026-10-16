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

static bool ends_with_ci(const char *s, size_t n, const char *suffix) {
    size_t m = strlen(suffix);
    if (n < m) return false;
    for (size_t i = 0; i < m; i++) {
        char a = s[n - m + i], b = suffix[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (a != b) return false;
    }
    return true;
}

size_t canvas_node_dir_links(const CanvasNode *n, const char *canvas_path, char ***out) {
    *out = NULL;
    size_t count = 0, cap = 0;
    const char *single = NULL;
    if (n->type == CNODE_FILE) single = n->file;
    else if (n->type != CNODE_TEXT || !n->text) return 0;
    if (n->type == CNODE_FILE && !single) return 0;

    const char *p = single ? NULL : n->text;
    for (;;) {
        const char *target;
        size_t len;
        if (single) {
            target = single;
            len = strlen(single);
        } else {
            p = strstr(p, "[[");
            if (!p) break;
            p += 2;
            const char *end = strstr(p, "]]");
            if (!end) break;
            len = 0;
            while (p + len < end && p[len] != '|' && p[len] != '#') len++;
            target = p;
            p = end + 2;
        }
        if (len > 0 && !ends_with_ci(target, len, ".canvas")) {
            char *raw = (char *)malloc(len + 1);
            memcpy(raw, target, len);
            raw[len] = '\0';
            char *path = project_resolve_link(canvas_path, raw);
            free(raw);
            bool dup = false;
            for (size_t i = 0; i < count; i++) dup = dup || strcmp((*out)[i], path) == 0;
            if (!dup && path_is_dir(path)) {
                if (count == cap) {
                    cap = cap ? cap * 2 : 4;
                    *out = (char **)realloc(*out, cap * sizeof(char *));
                }
                (*out)[count++] = path;
            } else {
                free(path);
            }
        }
        if (single) break;
    }
    return count;
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

static bool get_num(struct json_object_s *obj, const char *key, double *out) {
    struct json_value_s *v = obj_get(obj, key);
    struct json_number_s *n = v ? json_value_as_number(v) : NULL;
    if (!n) return false;
    char tmp[64];
    size_t len = n->number_size < sizeof(tmp) - 1 ? n->number_size : sizeof(tmp) - 1;
    memcpy(tmp, n->number, len);
    tmp[len] = '\0';
    char *end;
    double d = strtod(tmp, &end);
    if (end == tmp) return false;
    *out = d;
    return true;
}

/* The optional "cost" object; anything missing or unusable keeps its default. */
static void read_cost(struct json_object_s *obj, CostParams *c) {
    cost_params_default(c);
    struct json_value_s *v = obj_get(obj, "cost");
    struct json_object_s *co = v ? json_value_as_object(v) : NULL;
    if (!co) return;
    double d;
    if (get_num(co, "salary", &d) && d >= 0) c->salary = d;
    if (get_num(co, "overhead", &d) && d > 0) c->overhead = d;
    if (get_num(co, "ai_share", &d) && d >= 0 && d <= 1) c->ai_share = d;
    if (get_num(co, "token_price", &d) && d >= 0) c->token_price = d;
    if (get_num(co, "reasoning_multiplier", &d) && d > 0) c->reasoning_multiplier = d;
    if (get_num(co, "chars_per_token", &d) && d > 0) c->chars_per_token = d;
    char *mode = get_str(co, "mode", NULL);
    if (mode) {
        cocomo_mode_parse(mode, &c->mode);
        free(mode);
    }
    char *currency = get_str(co, "currency", NULL);
    if (currency) {
        snprintf(c->currency, sizeof(c->currency), "%s", currency);
        free(currency);
    }
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
    read_cost(obj, &out->cost);
    free(root);
    free(buf);

    out->root_path = project_resolve_link(out->path, out->root);
    if (!path_exists(out->root_path) && !canvas_doc_write_empty(out->root_path)) {
        project_free(out);
        return false;
    }
    return true;
}

static bool write_project(const char *path, const char *title, const char *description, const char *root,
                          const char *created, const CostParams *c) {
    FILE *f = fopen(path, "w");
    if (!f) return false;
    fputs("{\n  \"title\": ", f);
    write_escaped(f, title);
    fputs(",\n  \"description\": ", f);
    write_escaped(f, description);
    fputs(",\n  \"root\": ", f);
    write_escaped(f, root);
    if (created) {
        fputs(",\n  \"created\": ", f);
        write_escaped(f, created);
    }
    fputs(",\n  \"cost\": {\n    \"currency\": ", f);
    write_escaped(f, c->currency);
    fprintf(f, ",\n    \"salary\": %.15g,\n    \"overhead\": %.15g,\n    \"mode\": ", c->salary, c->overhead);
    write_escaped(f, cocomo_mode_name(c->mode));
    fprintf(f,
            ",\n    \"ai_share\": %.15g,\n    \"token_price\": %.15g,\n    \"reasoning_multiplier\": %.15g,"
            "\n    \"chars_per_token\": %.15g",
            c->ai_share, c->token_price, c->reasoning_multiplier, c->chars_per_token);
    fputs("\n  }\n}\n", f);
    return fclose(f) == 0;
}

bool project_save(const Project *p) {
    return write_project(p->path, p->title, p->description ? p->description : "", p->root, p->created, &p->cost);
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

    CostParams cost;
    cost_params_default(&cost);
    bool ok = write_project(path, title && *title ? title : "Untitled", "", "root.canvas", created, &cost);

    ok = ok && project_open(path, out); /* creates root.canvas */
    free(path);
    return ok;
}
