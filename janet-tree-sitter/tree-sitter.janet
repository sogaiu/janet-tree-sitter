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

  (let [buf @""]
    (with-dyns [:out buf]
      (print-s-expr "(def a 1)" "clojure")
      (string buf)))
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

(defn query
  "Return new query for `lang-name` and `src`."
  [lang-name src]
  # XXX: better to reuse an existing lang instead of creating new parser?
  (def p
    (init lang-name))
  (assert p "Parser init failed")
  (def lang
    (:language p))
  # XXX
  (_tree-sitter/_query lang src))

(comment

  # XXX
  (query "clojure" "_ @any")

  )

(defn query-cursor
  "Return new query cursor."
  []
  (_tree-sitter/_query-cursor))

(comment

  (def src "(def a 8)")

  (def p (init "clojure"))
  (assert p "Parser init failed")
  #
  (def t (:parse-string p src))
  (def rn (:root-node t))

  (def q
    (query "clojure" "_ @any"))

  (assert q "Query creation failed")

  (def qc (query-cursor))

  (assert qc "Query cursor creation failed")

  (:exec qc q rn)

  (:next-match qc)

  )

(defn query-and-report
  ``
  Perform `qry` on `src` and report results.

  `qry` should be a string in tree-sitter's query syntax.

  `src` should be a string in language `lang-name`.

  Optional arg `so-path` is a path to a parser shared object.
  ``
  [qry src lang-name &opt so-path]
  (def p
    (init lang-name so-path))
  (assert p "Parser init failed")
  #
  (def t (:parse-string p src))
  (def rn (:root-node t))

  (def q
    (query "clojure" qry))

  (assert q "Query creation failed")

  (def qc (query-cursor))

  (assert qc "Query cursor creation failed")

  (:exec qc q rn)

  (while true
    (def m
      (:next-match qc))
    (unless m
      (break))
    (def [id p_idx caps]
      m)
    (each [idx node] caps
      (printf "  pattern: %d" p_idx)
      (def [s-row s-col] (:start-point node))
      (def [e-row e-col] (:end-point node))
      (def [name _] (:capture-name-for-id q idx))
      (if (= s-row e-row)
        (printf (string "    "
                        "capture: %d - %s, "
                        "start: (%d, %d), "
                        "end: (%d, %d), "
                        "text: `%s`")
                idx name
                s-row s-col
                e-row e-col
                (:text node src))
        (printf (string "    "
                        "capture: %s, "
                        "start: (%d, %d), "
                        "end: (%d, %d)")
                name
                s-row s-col
                e-row e-col)))))

(defn search-dfs
  [root-node pred-fn &opt named-only? depth-limit]
  (default named-only? true)
  (default depth-limit 500)
  (var target nil)
  #
  (defn helper
    [root pred &opt named limit]
    #
    (def node root)
    #
    (when (pred node)
      (set target node)
      (break true))
    #
    (when (neg? limit)
      (break false))
    #
    (def count
      (if named
        (:named-child-count node)
        (:child-count node)))
    (var idx 0)
    (var done false)
    (while (and (not done)
                (< idx count))
      (def child
        (if named
          (:named-child node idx)
          (:child node idx)))
      (if (and child
               (not (:is-null child))
               (helper child pred named (dec limit)))
        # XXX: unwinding here?
        (set done true)
        (++ idx)))
    #
    done)
  #
  (when (helper root-node pred-fn named-only? depth-limit)
    target))

(comment

  (def src
    ``
    (def my-fn
      [x]
      (+ x 1
           (/ 6 3)))
    ``)

  (when-let [p (try
                 (init "janet-simple")
                 ([err]
                   (eprint err)
                   nil))
             t (:parse-string p src)
             rn (:root-node t)
             target (search-dfs rn
                                (fn [node]
                                  (and (= (:type node)
                                          "sym_lit")
                                       (= (:text node src)
                                          "+"))))]
    (when target
      [(:type target)
       (:text target src)
       (:start-byte target)
       (:end-byte target)]))
  # =>
  ["sym_lit" "+" 20 21]

  (when-let [p (try
                 (init "janet-simple")
                 ([err]
                   (eprint err)
                   nil))
             t (:parse-string p src)
             rn (:root-node t)
             target (search-dfs rn
                                (fn [node]
                                  (and (= (:type node)
                                          "num_lit")
                                       (= (:text node src)
                                          "3"))))]
    (when target
      [(:type target)
       (:text target src)
       (:start-byte target)
       (:end-byte target)]))
  # =>
  ["num_lit" "3" 38 39]

  (when-let [p (try
                 (init "janet-simple")
                 ([err]
                   (eprint err)
                   nil))
             t (:parse-string p src)
             rn (:root-node t)
             target (search-dfs rn
                                (fn [node]
                                  (and (= (:type node)
                                          "num_lit")
                                       (= (:text node src)
                                          "2"))))]
    (when target
      [(:type target)
       (:text target src)
       (:start-byte target)
       (:end-byte target)]))
  # =>
  nil

  )

