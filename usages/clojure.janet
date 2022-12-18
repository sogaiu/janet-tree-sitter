(import ../janet-tree-sitter/tree-sitter)

(comment

  (def src "{:a 1 :b [:x :y :z]}")

  (def p (tree-sitter/init "clojure"))

  (def t (:parse-string p src))

  (def rn (:root-node t))

  (:has-error rn)
  # => false

  (def kn (:child (:child rn 0) 1))

  (:text kn src)
  # =>
  ":a"

  (:is-named kn)
  # =>
  true

  (:type kn)
  # =>
  "kwd_lit"

  (:expr kn)
  # =>
  "(kwd_lit)"

  (:eq rn (:parent (:parent kn)))
  # =>
  true

  (:start-byte kn)
  # =>
  1

  (:end-byte kn)
  # =>
  3

  (= (- (:end-byte kn) (:start-byte kn))
     (length (:text kn src)))
  # =>
  true

  (:eq kn (:prev-sibling (:next-sibling kn)))
  # =>
  true

  (:is-named (:prev-sibling kn))
  # =>
  false

  (:text (:prev-sibling kn) src)
  # =>
  "{"

  (def vn (:next-sibling (:next-sibling (:next-sibling kn))))

  (:type vn)
  # =>
  "vec_lit"

  (:expr vn)
  # =>
  "(vec_lit value: (kwd_lit) value: (kwd_lit) value: (kwd_lit))"

  )

(comment

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

  (:text (:first-child-for-byte rn (dec (length txt)))
         src)
  # =>
  "(def x a)"

  # XXX: this may be an issue with tree-sitter -- result should be non-nil
  #      https://github.com/tree-sitter/tree-sitter-bash/issues/139
  (:first-child-for-byte rn (length txt))
  # = > nil

  # XXX: this may be an issue with tree-sitter -- result should be non-nil
  #      https://github.com/tree-sitter/tree-sitter-bash/issues/139
  (:first-child-for-byte rn (+ 1 (length txt)))
  # = > nil

  (:text (:first-child-for-byte rn (+ 2 (length txt)))
         src)
  # =>
  "{def y 2}"

 )
