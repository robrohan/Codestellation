; Documentation only, not compiled/used at runtime -- see the header
; comment in lisp_adapter.c for why.
;
; SWAP GRAMMAR + QUERIES WHEN THE ACTUAL LISP DIALECT IN USE IS CONFIRMED.
; This adapter is a Common Lisp stand-in: tree-sitter-grammars/tree-sitter-
; commonlisp is WIP, extends the tree-sitter-clojure grammar, and has no
; Common-Lisp-specific node types -- everything is generic s-expression
; shape (list_lit / sym_lit / kwd_lit / str_lit). There is nothing here
; resembling `(defpackage ...)` or `(in-package ...)` as named grammar
; rules to query against; matching has to be done procedurally on
; head-symbol text, which is what lisp_adapter.c actually does.
;
; If the dialect turns out to be Emacs Lisp instead, Wilfred/tree-sitter-
; elisp is the recommended swap-in (more mature, actively maintained).
(list_lit . (sym_lit) @head)
