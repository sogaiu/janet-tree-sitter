(import ./_tree-sitter)
(import ./path :prefix "")

(defn lang-name-to-path
  [lang-name]
  (var tree-sitter-dir (os/getenv "TREE_SITTER_DIR"))
  (unless tree-sitter-dir
    (if-let [home (os/getenv "HOME")]
      (set tree-sitter-dir (path/join home ".tree-sitter"))
      (when-let [user-profile (os/getenv "USERPROFILE")]
        (set tree-sitter-dir (path/join user-profile ".tree-sitter"))))
    (unless tree-sitter-dir
      (break)))
  (def bin-dir
    (path/join tree-sitter-dir "bin"))
  # XXX: check appropriateness
  (def lang-name-norm
    (string/replace-all "-" "_" lang-name))
  (def ext
    # XXX: modify as necessary
    (case (os/which)
      :windows
      ".dll"
      :macos
      ".dylib"
      #
      ".so"))
  (path/join bin-dir
             (string lang-name-norm ext)))

(comment

  (let [base (path/basename (lang-name-to-path "clojure"))]
    (and (string/has-prefix? "clojure" base)
         (or (string/has-suffix? ".so" base)
             (string/has-suffix? ".dll" base))))
  # =>
  true

  (let [base (path/basename (lang-name-to-path "janet_simple"))]
    (and (string/has-prefix? "janet_simple" base)
         (or (string/has-suffix? ".so" base)
             (string/has-suffix? ".dll" base))))
  # =>
  true

  (let [base (path/basename (lang-name-to-path "janet-simple"))]
    (and (string/has-prefix? "janet_simple" base)
         (or (string/has-suffix? ".so" base)
             (string/has-suffix? ".dll" base))))
  # =>
  true

  )

(defn lang-name-to-fn-name
  [lang-name]
  # XXX: check appropriateness
  (def lang-name-norm
    (string/replace-all "-" "_" lang-name))
  (string "tree_sitter_" lang-name-norm))

(comment

  (lang-name-to-fn-name "clojure")
  # =>
  "tree_sitter_clojure"

  (lang-name-to-fn-name "janet_simple")
  # =>
  "tree_sitter_janet_simple"

  (lang-name-to-fn-name "janet-simple")
  # =>
  "tree_sitter_janet_simple"

  )

(defn init
  ``
  Return tree-sitter parser for grammar.
  `lang-name` identifies a specific grammar, e.g.
  `clojure` or `janet_simple`.

  ``
  [lang-name &opt so-path]
  (def fn-name
    (lang-name-to-fn-name lang-name))
  (default so-path
    (lang-name-to-path lang-name))
  (_tree-sitter/_init so-path fn-name))

(comment

  (def src "(def a 1)")

  (when-let [p (try
                 (init "clojure")
                 ([err]
                   (eprint err)
                   nil))
             t (:parse-string p src)
             rn (:root-node t)]
    (:text rn src))
  # =>
  src

  (when-let [p (try
                 (init "janet-simple")
                 ([err]
                   (eprint err)
                   nil))
             t (:parse-string p src)
             rn (:root-node t)]
    (:text rn src))
  # =>
  src

  )

(defn cursor
  "Return new cursor for `node`."
  [node]
  (_tree-sitter/_cursor node))

(defn print-s-expr
  ``
  Print s-expression representation of `src`.

  `src` should be a string in language `lang-name`.

  Optional arg `so-path` is a path to a parser shared object.
  ``
  [src lang-name &opt so-path]
  (def p
    (init lang-name so-path))
  (assert p "Parser init failed")
  #
  (def t (:parse-string p src))
  (def rn (:root-node t))
  (def curs (cursor rn))
  #
  (var needs-nl nil)
  (var indent-lvl 0)
  (var visited-kids? nil)
  #
  (while true
    (def node (:node curs))
    (def named? (:is-named node))
    (if visited-kids?
      (do
        (when named?
          (prin ")")
          (set needs-nl true))
        (cond
          (:go-next-sibling curs)
          (set visited-kids? false)
          #
          (:go-parent curs)
          (do
            (set visited-kids? true)
            (-- indent-lvl))
          #
          (break)))
      (do
        (when named?
          (when needs-nl
            (print))
          (for i 0 indent-lvl
            (prin "  "))
          (def [start-row start-col]
            (:start-point node))
          (def [end-row end-col]
            (:end-point node))
          (def field-name
            (:field-name curs))
          (when field-name
            (prinf "%s: " field-name))
          (prinf "(%s [%d, %d] - [%d, %d]"
                 (:type node)
                 start-row
                 start-col
                 end-row
                 end-col)
          (set needs-nl true))
        (if (:go-first-child curs)
          (do
            (set visited-kids? false)
            (++ indent-lvl))
          (set visited-kids? true))))))

(comment

  (def res
    (let [buf @""]
      (with-dyns [:out buf]
        (print-s-expr "(def a 1)" "clojure")
        (string buf))))
  # =>
  ``
  (source [0, 0] - [0, 9]
    (list_lit [0, 0] - [0, 9]
      value: (sym_lit [0, 1] - [0, 4]
        name: (sym_name [0, 1] - [0, 4]))
      value: (sym_lit [0, 5] - [0, 6]
        name: (sym_name [0, 5] - [0, 6]))
      value: (num_lit [0, 7] - [0, 8])))
  ``

  )
