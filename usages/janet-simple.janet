(import ../janet-tree-sitter/tree-sitter)

(comment

  (def src "(defn my-fn [x] (+ x 1))")

  (def p (tree-sitter/init "janet_simple"))

  (def t (:parse-string p src))

  (def rn (:root-node t))

  (:has-error rn)
  # => false

  (def sn (:child (:child rn 0) 1))

  (:text sn src)
  # => "defn"

  (:is-named sn)
  # => true

  (:type sn)
  # => "sym_lit"

  (:expr sn)
  # => "(sym_lit)"

  (:eq rn (:parent (:parent sn)))
  # => true

  (:start-byte sn)
  # => 1
  
  (:end-byte sn)
  # => 5

  (= (- (:end-byte sn) (:start-byte sn))
     (length (:text sn src)))
  # => true

  (:eq sn (:prev-sibling (:next-sibling sn)))
  # => true

  (:is-named (:prev-sibling sn))
  # => false

  (:text (:prev-sibling sn) src)
  # => "("

  (def sqtn (:next-sibling (:next-sibling sn)))

  (:type sqtn)
  # => "sqr_tup_lit"

  (:expr sqtn)
  # => "(sqr_tup_lit (sym_lit))"

  )
