#ifndef CODEMAP_ADAPTER_H
#define CODEMAP_ADAPTER_H

#include <stdint.h>
#include <tree_sitter/api.h>

/* A parsed source file. `source`/`tree` live for the lifetime of the
 * ParsedFileList that owns this entry (see pipeline/parse.h). */
typedef struct {
    const char *path;
    TSTree     *tree;
    const char *source;
    uint32_t    source_len;
} ParsedFile;

typedef enum { DECL_TYPE, DECL_NAMESPACE, DECL_PACKAGE } DeclKind;
typedef struct {
    DeclKind    kind;
    const char *qualified_name;
} DeclFact;

typedef enum { REF_TYPE_NAME, REF_IMPORT_PATH, REF_MODULE_REQUIRE } RefKind;
typedef struct {
    RefKind     kind;
    const char *raw_text;        /* e.g. "Widgets.Button", "foo.h", "my-lib" */
    const char *scope_namespace; /* enclosing namespace/package at the ref site, or NULL */
} RefFact;

/* Sinks only see the fact for the duration of the call — anything the
 * receiver needs to keep must be copied before returning. */
typedef void (*DeclSinkFn)(void *ctx, const DeclFact *fact);
typedef void (*RefSinkFn)(void *ctx, const RefFact *fact);

typedef struct LanguageAdapter {
    const char  *name;
    const char **extensions;      /* NULL-terminated, e.g. {".cs", NULL} */
    int          extension_count;

    const TSLanguage *(*ts_language)(void);
    TSQuery *decl_query; /* may be NULL if extract_declarations doesn't need one */
    TSQuery *ref_query;  /* may be NULL if extract_references doesn't need one */

    void (*extract_declarations)(const ParsedFile *file, TSQuery *decl_query,
                                  DeclSinkFn sink, void *ctx);
    void (*extract_references)(const ParsedFile *file, TSQuery *ref_query,
                                RefSinkFn sink, void *ctx);

    /* Turn a raw reference into a best-guess qualified name matching some
     * DeclFact.qualified_name emitted elsewhere in the codebase, or NULL if
     * it can't be resolved (e.g. a system/external include). Returns a
     * malloc'd string owned by the caller, or NULL. */
    char *(*resolve_reference)(const char *raw_text,
                                const char *referencing_file_path,
                                const char *scope_namespace,
                                const char **imported_namespaces,
                                int imported_count,
                                void *symbol_table_handle);
} LanguageAdapter;

#endif
