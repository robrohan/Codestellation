(in-package :codemap.d)

(require :codemap.b)               ; -> package-b.lisp

(defun run-later ()
  (codemap.b:run))
