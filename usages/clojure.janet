(import ../janet-tree-sitter/tree-sitter)

(comment

  (def src "{:a 1 :b [:x :y :z]}")

  (def p (tree-sitter/init "clojure"))

  (assert p "Parser init failed")

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
  "(kwd_lit name: (kwd_name))"

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
  (string "(vec_lit "
          "value: (kwd_lit name: (kwd_name)) "
          "value: (kwd_lit name: (kwd_name)) "
          "value: (kwd_lit name: (kwd_name)))")

  )

(comment

  (def src "[:x :y :z]")

  (def p (tree-sitter/init "clojure"))

  (assert p "Parser init failed")

  (def t (:parse-string p src))

  (def rn (:root-node t))

  (def c (tree-sitter/cursor rn))

  (:expr (:current-node c))
  # =>
  (string "(source "
          "(vec_lit "
          "value: (kwd_lit name: (kwd_name)) "
          "value: (kwd_lit name: (kwd_name)) "
          "value: (kwd_lit name: (kwd_name))))")

  (:go-first-child c)
  # =>
  true

  (:go-first-child c)
  # =>
  true

  (:text (:current-node c) src)
  # =>
  "["

  (:go-parent c)
  # =>
  true

  (:text (:current-node c) src)
  # =>
  "[:x :y :z]"

  (:go-first-child c)
  # =>
  true

  (:go-next-sibling c)
  # =>
  true

  (:go-next-sibling c)
  # =>
  true

  (:go-next-sibling c)
  # =>
  true

  (:text (:current-node c) src)
  # =>
  ":z"

  (:reset c rn)
  # =>
  nil

  (:text (:current-node c) src)
  # =>
  "[:x :y :z]"

  )
