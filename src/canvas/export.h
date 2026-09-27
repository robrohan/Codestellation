#ifndef CODEMAP_EXPORT_H
#define CODEMAP_EXPORT_H

#include <stdbool.h>
#include "project.h"

/* Writes a whole project as one Markdown document: a depth-first walk
 * from the root canvas, each canvas a section (children right after their
 * parent), each box a subsection with its markdown, groups nesting the
 * boxes inside them, edges listed as connections, weak links as
 * references. Boxes with folder links get a code summary from that folder
 * set's cached graph (languages, file count, most-depended-on files) plus
 * any file/group notes -- or a line saying it hasn't been built yet.
 *
 * EXPORT_MANUAL reads as documentation. EXPORT_LLM is the same content
 * framed as context for a language model: a preface explaining the
 * structure, and every component heading carrying its full breadcrumb
 * trail so each section stands on its own. */
typedef enum { EXPORT_MANUAL, EXPORT_LLM } ExportStyle;

bool export_project(const Project *p, ExportStyle style, const char *out_path);

#endif
