#ifndef CODEMAP_REGISTRY_H
#define CODEMAP_REGISTRY_H

#include "adapter.h"

void adapter_registry_init(void);
const LanguageAdapter *adapter_for_extension(const char *ext);

#endif
