#include "resolve.h"
#include "../common/symtab.h"
#include "../common/pathutil.h"
#include "../lang/adapter.h"
#include <stdlib.h>
#include <string.h>

typedef struct { SymbolTable *table; int file_id; } DeclCtx;
typedef struct { RefFact *items; size_t count, cap; } RefList;

static void decl_sink(void *ctx_, const DeclFact *fact) {
    DeclCtx *ctx = (DeclCtx *)ctx_;
    symtab_put(ctx->table, fact->qualified_name, ctx->file_id);
}

static void reflist_add(RefList *list, const RefFact *fact) {
    if (list->count == list->cap) {
        list->cap = list->cap ? list->cap * 2 : 8;
        list->items = (RefFact *)realloc(list->items, list->cap * sizeof(RefFact));
    }
    RefFact copy = *fact;
    /* Sink contract: fields are only valid for the call's duration, so copy
     * anything we need to keep past it. */
    copy.raw_text = fact->raw_text ? xstrdup(fact->raw_text) : NULL;
    copy.scope_namespace = fact->scope_namespace ? xstrdup(fact->scope_namespace) : NULL;
    list->items[list->count++] = copy;
}

static void ref_sink(void *ctx_, const RefFact *fact) {
    reflist_add((RefList *)ctx_, fact);
}

static const char *kind_label(RefKind k) {
    switch (k) {
        case REF_TYPE_NAME:    return "type_reference";
        case REF_IMPORT_PATH:  return "import";
        case REF_MODULE_REQUIRE: return "require";
    }
    return "reference";
}

bool resolve_build_graph(const ParsedFileList *files, Graph *out_graph, int *out_unresolved) {
    SymbolTable *table = symtab_create(64);

    /* Add nodes first, in file order, so node id == index into files->entries. */
    for (size_t i = 0; i < files->count; i++) {
        graph_add_node(out_graph, files->entries[i].parsed.path, files->entries[i].adapter->name);
    }

    /* Pass 1: declarations. */
    for (size_t i = 0; i < files->count; i++) {
        const ParsedFileEntry *entry = &files->entries[i];
        DeclCtx ctx = { table, (int)i };
        entry->adapter->extract_declarations(&entry->parsed, entry->adapter->decl_query, decl_sink, &ctx);
    }

    /* Pass 2: references -> edges. */
    int unresolved = 0;
    for (size_t i = 0; i < files->count; i++) {
        const ParsedFileEntry *entry = &files->entries[i];
        RefList refs;
        memset(&refs, 0, sizeof(refs));
        entry->adapter->extract_references(&entry->parsed, entry->adapter->ref_query, ref_sink, &refs);

        const char **imports = (const char **)malloc(sizeof(char *) * (refs.count + 1));
        int import_count = 0;
        for (size_t r = 0; r < refs.count; r++) {
            if (refs.items[r].kind == REF_IMPORT_PATH || refs.items[r].kind == REF_MODULE_REQUIRE) {
                imports[import_count++] = refs.items[r].raw_text;
            }
        }

        for (size_t r = 0; r < refs.count; r++) {
            RefFact *ref = &refs.items[r];
            char *candidate = entry->adapter->resolve_reference(
                ref->raw_text, entry->parsed.path, ref->scope_namespace,
                imports, import_count, table);
            if (candidate) {
                int target_id = symtab_get(table, candidate);
                if (target_id >= 0) {
                    if (target_id != (int)i) {
                        graph_add_edge(out_graph, (int)i, target_id, kind_label(ref->kind));
                    }
                } else {
                    unresolved++;
                }
                free(candidate);
            } else {
                unresolved++;
            }
        }

        free(imports);
        for (size_t r = 0; r < refs.count; r++) {
            free((void *)refs.items[r].raw_text);
            free((void *)refs.items[r].scope_namespace);
        }
        free(refs.items);
    }

    if (out_unresolved) *out_unresolved = unresolved;
    symtab_destroy(table);
    return true;
}
