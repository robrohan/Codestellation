#ifndef CODEMAP_JS_ADAPTER_H
#define CODEMAP_JS_ADAPTER_H

#include "../adapter.h"

/* JavaScript (.js/.mjs/.cjs/.jsx), TypeScript (.ts/.mts/.cts) and TSX
 * (.tsx) -- three grammars, one shared implementation. */
const LanguageAdapter *javascript_adapter_get(void);
const LanguageAdapter *typescript_adapter_get(void);
const LanguageAdapter *tsx_adapter_get(void);

/* Forgets tsconfig/jsconfig files read during the previous build, so edits
 * to them are picked up. Called by adapter_registry_init at the start of
 * every build. */
void js_adapter_reset(void);

#endif
