#include "registry.h"
#include "c/c_adapter.h"
#include "csharp/csharp_adapter.h"
#include <string.h>

#define MAX_ADAPTERS 16

static const LanguageAdapter *g_adapters[MAX_ADAPTERS];
static int g_adapter_count = 0;

void adapter_registry_init(void) {
    g_adapter_count = 0;
    g_adapters[g_adapter_count++] = c_adapter_get();
    g_adapters[g_adapter_count++] = csharp_adapter_get();
}

const LanguageAdapter *adapter_for_extension(const char *ext) {
    for (int i = 0; i < g_adapter_count; i++) {
        const LanguageAdapter *a = g_adapters[i];
        for (int j = 0; j < a->extension_count; j++) {
            if (strcmp(a->extensions[j], ext) == 0) return a;
        }
    }
    return NULL;
}
