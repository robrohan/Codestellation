#ifndef CODEMAP_SYMTAB_H
#define CODEMAP_SYMTAB_H

#include <stddef.h>

/* Simple open-addressing string -> file-id map. Used as the cross-file
 * symbol table: every adapter's declarations go in (pass 1), every
 * resolved reference is looked up (pass 2). */
typedef struct SymbolTable SymbolTable;

SymbolTable *symtab_create(size_t initial_capacity);
void symtab_put(SymbolTable *t, const char *key, int file_id);
int  symtab_get(const SymbolTable *t, const char *key); /* -1 if not found */
void symtab_destroy(SymbolTable *t);

#endif
