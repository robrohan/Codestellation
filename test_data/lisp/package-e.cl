(defpackage :codemap.e
  (:use :codemap.c))               ; -> package-c.lisp

(in-package :codemap.e)

(defun run-all ()
  (run-both))
