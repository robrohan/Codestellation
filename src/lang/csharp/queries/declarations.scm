; Documentation only -- the actual C# adapter walks the tree directly
; (csharp_adapter.c) rather than using these as compiled tree-sitter
; queries, since correctly tracking enclosing-namespace/nested-type scope
; while extracting declarations needs real recursion, not a flat query
; match list. Kept here to describe the node shapes involved.

(namespace_declaration name: (_) @namespace.name)
(file_scoped_namespace_declaration name: (_) @namespace.name)
(class_declaration name: (identifier) @type.decl)
(interface_declaration name: (identifier) @type.decl)
(struct_declaration name: (identifier) @type.decl)
(enum_declaration name: (identifier) @type.decl)
