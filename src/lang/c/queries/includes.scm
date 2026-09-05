; Local #include "foo.h" directives are the only dependency signal C has —
; the C adapter treats each one as a direct file-path reference (no symbol
; table needed). System includes (#include <...>) are outside the project
; and intentionally not captured here.
(preproc_include path: (string_literal) @path.local)
