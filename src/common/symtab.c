#include "symtab.h"
#include "pathutil.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

typedef struct { char *key; int file_id; int occupied; } Entry;

struct SymbolTable {
    Entry *entries;
    size_t capacity;
    size_t count;
};

static uint64_t fnv1a(const char *s) {
    uint64_t h = 1469598103934665603ULL;
    for (; *s; s++) {
        h ^= (unsigned char)*s;
        h *= 1099511628211ULL;
    }
    return h;
}

SymbolTable *symtab_create(size_t initial_capacity) {
    SymbolTable *t = (SymbolTable *)malloc(sizeof(SymbolTable));
    t->capacity = initial_capacity < 8 ? 8 : initial_capacity;
    t->entries = (Entry *)calloc(t->capacity, sizeof(Entry));
    t->count = 0;
    return t;
}

static void symtab_resize(SymbolTable *t, size_t new_cap) {
    Entry *old_entries = t->entries;
    size_t old_cap = t->capacity;

    t->entries = (Entry *)calloc(new_cap, sizeof(Entry));
    t->capacity = new_cap;
    t->count = 0;

    for (size_t i = 0; i < old_cap; i++) {
        if (old_entries[i].occupied) {
            symtab_put(t, old_entries[i].key, old_entries[i].file_id);
            free(old_entries[i].key);
        }
    }
    free(old_entries);
}

void symtab_put(SymbolTable *t, const char *key, int file_id) {
    if ((t->count + 1) * 10 >= t->capacity * 7) {
        symtab_resize(t, t->capacity * 2);
    }
    uint64_t h = fnv1a(key);
    size_t idx = h % t->capacity;
    while (t->entries[idx].occupied) {
        if (strcmp(t->entries[idx].key, key) == 0) {
            t->entries[idx].file_id = file_id;
            return;
        }
        idx = (idx + 1) % t->capacity;
    }
    t->entries[idx].key = xstrdup(key);
    t->entries[idx].file_id = file_id;
    t->entries[idx].occupied = 1;
    t->count++;
}

int symtab_get(const SymbolTable *t, const char *key) {
    uint64_t h = fnv1a(key);
    size_t idx = h % t->capacity;
    size_t probes = 0;
    while (t->entries[idx].occupied && probes < t->capacity) {
        if (strcmp(t->entries[idx].key, key) == 0) return t->entries[idx].file_id;
        idx = (idx + 1) % t->capacity;
        probes++;
    }
    return -1;
}

void symtab_destroy(SymbolTable *t) {
    for (size_t i = 0; i < t->capacity; i++) {
        if (t->entries[i].occupied) free(t->entries[i].key);
    }
    free(t->entries);
    free(t);
}
