#ifndef CODEMAP_LISP_ADAPTER_H
#define CODEMAP_LISP_ADAPTER_H

#include "../adapter.h"

/* Common Lisp stand-in -- see the header comment in lisp_adapter.c and
 * queries/deps.scm before trusting this on a real dialect you haven't
 * confirmed yet. */
const LanguageAdapter *lisp_adapter_get(void);

#endif
