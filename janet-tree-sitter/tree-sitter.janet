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
  [lang-name]
  (when-let [path (lang-name-to-path lang-name)
             fn-name (lang-name-to-fn-name lang-name)]
    (_tree-sitter/_init path fn-name)))

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
