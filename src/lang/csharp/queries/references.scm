; Documentation only -- see declarations.scm. `using_directive` has no
; direct field for its target (only an optional `name` field for an
; alias); the real target is the directive's other/last child, which is
; one of the grammar's `type` supertype's concrete alternatives
; (identifier, qualified_name, generic_name, ...). `base_list` is not a
; field on class/interface/struct/enum_declaration -- it's an unnamed
; child scanned for by node type.

(using_directive (_) @import.path)
(base_list (_) @type.ref)
