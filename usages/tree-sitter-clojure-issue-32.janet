(import ../janet-tree-sitter/tree-sitter)

(comment

  # https://github.com/sogaiu/tree-sitter-clojure/issues/139
  (def src
    ``
    (def x a)

    {def y 2}

    [def z 3]
    ``)

  (def p (tree-sitter/init "clojure"))

  (def t (:parse-string p src))

  (def rn (:root-node t))

  (:has-error rn)
  # =>
  false

  (def txt
    (:text (:first-child-for-byte rn 0) src))

  txt
  # =>
  "(def x a)"

  # here results are ok
  (:text (:first-child-for-byte rn (- (length txt) 1))
         src)
  # =>
  "(def x a)"

  # XXX: result should be non-nil
  (:first-child-for-byte rn (length txt))
  # = > nil

  # XXX: result should be non-nil
  (:first-child-for-byte rn (+ (length txt) 1))
  # = > nil

  # results are ok after problematic indeces...
  (:text (:first-child-for-byte rn (+ (length txt) 2))
         src)
  # =>
  "{def y 2}"

 )
