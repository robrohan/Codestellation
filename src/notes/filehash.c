#include "filehash.h"
#include <stdio.h>
#include <string.h>

bool file_content_hash(const char *path, uint64_t *out_hash) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;

    uint64_t h = 1469598103934665603ULL;
    unsigned char buf[4096];
    size_t got;
    while ((got = fread(buf, 1, sizeof(buf), f)) > 0) {
        for (size_t i = 0; i < got; i++) {
            h ^= buf[i];
            h *= 1099511628211ULL;
        }
    }
    fclose(f);

    *out_hash = h;
    return true;
}

void file_hash_to_hex(uint64_t hash, char out_hex[17]) {
    static const char digits[] = "0123456789abcdef";
    for (int i = 15; i >= 0; i--) {
        out_hex[i] = digits[hash & 0xF];
        hash >>= 4;
    }
    out_hex[16] = '\0';
}

bool file_hash_from_hex(const char *hex, uint64_t *out_hash) {
    if (strlen(hex) != 16) return false;
    uint64_t h = 0;
    for (int i = 0; i < 16; i++) {
        unsigned char c = (unsigned char)hex[i];
        int digit;
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (c >= 'a' && c <= 'f') digit = 10 + (c - 'a');
        else if (c >= 'A' && c <= 'F') digit = 10 + (c - 'A');
        else return false;
        h = (h << 4) | (uint64_t)digit;
    }
    *out_hash = h;
    return true;
}
