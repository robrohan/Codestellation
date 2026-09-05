#ifndef CODEMAP_PARSE_H
#define CODEMAP_PARSE_H

#include <stdbool.h>
#include "walk.h"
#include "../lang/adapter.h"

typedef struct {
    ParsedFile parsed;
    const LanguageAdapter *adapter;
} ParsedFileEntry;

typedef struct {
    ParsedFileEntry *entries;
    size_t count, cap;
} ParsedFileList;

bool parse_all(const FileList *files, ParsedFileList *out);
void parsed_file_list_free(ParsedFileList *list);

#endif
