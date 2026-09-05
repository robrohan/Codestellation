#include "parse.h"
#include "../lang/registry.h"
#include "../common/pathutil.h"
#include <tree_sitter/api.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *xstrdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = (char *)malloc(n);
    memcpy(p, s, n);
    return p;
}

static char *read_file(const char *path, uint32_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0) { fclose(f); return NULL; }
    char *buf = (char *)malloc((size_t)size + 1);
    size_t got = fread(buf, 1, (size_t)size, f);
    buf[got] = '\0';
    fclose(f);
    if (out_len) *out_len = (uint32_t)got;
    return buf;
}

static void list_add(ParsedFileList *list, ParsedFileEntry entry) {
    if (list->count == list->cap) {
        list->cap = list->cap ? list->cap * 2 : 16;
        list->entries = (ParsedFileEntry *)realloc(list->entries, list->cap * sizeof(ParsedFileEntry));
    }
    list->entries[list->count++] = entry;
}

bool parse_all(const FileList *files, ParsedFileList *out) {
    memset(out, 0, sizeof(*out));

    for (size_t i = 0; i < files->count; i++) {
        const char *path = files->paths[i];
        const LanguageAdapter *adapter = adapter_for_extension(path_extension(path));
        if (!adapter) continue;

        uint32_t len = 0;
        char *source = read_file(path, &len);
        if (!source) {
            fprintf(stderr, "warning: could not read %s\n", path);
            continue;
        }

        TSParser *parser = ts_parser_new();
        ts_parser_set_language(parser, adapter->ts_language());
        TSTree *tree = ts_parser_parse_string(parser, NULL, source, len);
        ts_parser_delete(parser);

        if (!tree) {
            fprintf(stderr, "warning: failed to parse %s\n", path);
            free(source);
            continue;
        }

        char *norm_path = path_normalize(path);

        ParsedFileEntry entry;
        entry.parsed.path = norm_path ? norm_path : xstrdup(path);
        entry.parsed.tree = tree;
        entry.parsed.source = source;
        entry.parsed.source_len = len;
        entry.adapter = adapter;
        list_add(out, entry);
    }
    return true;
}

void parsed_file_list_free(ParsedFileList *list) {
    for (size_t i = 0; i < list->count; i++) {
        ts_tree_delete(list->entries[i].parsed.tree);
        free((void *)list->entries[i].parsed.source);
        free((void *)list->entries[i].parsed.path);
    }
    free(list->entries);
}
