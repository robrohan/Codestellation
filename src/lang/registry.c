#include "registry.h"
#include "c/c_adapter.h"
#include "csharp/csharp_adapter.h"
#include "lisp/lisp_adapter.h"
#include "python/python_adapter.h"
#include "go/go_adapter.h"
#include "php/php_adapter.h"
#include "vbnet/vbnet_adapter.h"
#include "js/js_adapter.h"
#include "json/json_adapter.h"
#include "bash/bash_adapter.h"
#include "proto/proto_adapter.h"
#include "sql/sql_adapter.h"
#include "powershell/powershell_adapter.h"
#include "batch/batch_adapter.h"
#include "html/html_adapter.h"
#include "css/css_adapter.h"
#include "markdown/markdown_adapter.h"
#include <string.h>

#define MAX_ADAPTERS 32

static const LanguageAdapter *g_adapters[MAX_ADAPTERS];
static int g_adapter_count = 0;

void adapter_registry_init(void) {
    js_adapter_reset();
    g_adapter_count = 0;
    g_adapters[g_adapter_count++] = c_adapter_get();
    g_adapters[g_adapter_count++] = csharp_adapter_get();
    g_adapters[g_adapter_count++] = lisp_adapter_get();
    g_adapters[g_adapter_count++] = python_adapter_get();
    g_adapters[g_adapter_count++] = go_adapter_get();
    g_adapters[g_adapter_count++] = php_adapter_get();
    g_adapters[g_adapter_count++] = vbnet_adapter_get();
    g_adapters[g_adapter_count++] = javascript_adapter_get();
    g_adapters[g_adapter_count++] = typescript_adapter_get();
    g_adapters[g_adapter_count++] = tsx_adapter_get();
    g_adapters[g_adapter_count++] = json_adapter_get();
    g_adapters[g_adapter_count++] = bash_adapter_get();
    g_adapters[g_adapter_count++] = proto_adapter_get();
    g_adapters[g_adapter_count++] = sql_adapter_get();
    g_adapters[g_adapter_count++] = powershell_adapter_get();
    g_adapters[g_adapter_count++] = batch_adapter_get();
    g_adapters[g_adapter_count++] = html_adapter_get();
    g_adapters[g_adapter_count++] = css_adapter_get();
    g_adapters[g_adapter_count++] = markdown_adapter_get();
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
