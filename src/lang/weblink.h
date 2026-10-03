#ifndef CODEMAP_WEBLINK_H
#define CODEMAP_WEBLINK_H

#include <stddef.h>

/* Shared by the HTML, CSS and Markdown adapters, which all point at other
 * files with URL-shaped links (href="css/app.css", url(base.css),
 * [guide](docs/guide.md#setup)) rather than language-level imports.
 *
 * Each of those files declares its own absolute path, the same key the
 * C, JS/TS, JSON, PHP, Python and shell adapters declare -- so a page or
 * a README can link straight to a script or source file. */

/* Turns a link as written into a local path, or NULL when it isn't one:
 * surrounding whitespace, quotes and <angle brackets> stripped; empty,
 * "#anchor"-only, scheme ("https:", "mailto:", "data:", ...) and
 * protocol-relative ("//cdn...") links dropped; "?query" and "#fragment"
 * cut; %XX decoded. Links to images, fonts and media are dropped too --
 * no adapter claims them, so they'd only inflate the unresolved count.
 * Caller frees. */
char *weblink_clean(const char *text, size_t len);

/* Resolves a cleaned link to a symbol-table key (malloc'd) or NULL.
 * Relative links are joined to the referencing file's directory. A
 * leading "/" is site-root relative, and the site root isn't known, so
 * it's tried as an absolute path and then against each ancestor directory
 * of the referencing file, nearest first. Each candidate is tried as
 * written, then with each of `try_suffixes` appended (NULL-terminated,
 * may be NULL), e.g. {".md", NULL} or {"index.html", NULL} for links
 * ending in "/". */
char *weblink_resolve(const char *link, const char *referencing_file_path,
                      const char *const *try_suffixes, void *symbol_table_handle);

#endif
