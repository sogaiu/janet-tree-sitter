(import ../janet-tree-sitter/tree-sitter)

(comment

  (def src "(defn my-fn [x] (+ x 1))")

  (def p (tree-sitter/init "janet_simple"))

  (assert p "Parser init failed")

  (def t (:parse-string p src))

  (def rn (:root-node t))

  (:has-error rn)
  # =>
  false

  (def pn (:child rn 0))

  (:child-count pn)
  # =>
  6

  (:named-child-count pn)
  # =>
  4

  (def sn (:child pn 1))

  (:eq sn (:named-child pn 0))
  # =>
  true

  (:eq sn (:descendant-for-byte-range rn 1 4))
  # =>
  true

  (:eq sn (:descendant-for-point-range rn 0 1 0 4))
  # =>
  true

  (:text sn src)
  # =>
  "defn"

  (:is-named sn)
  # =>
  true

  (:type sn)
  # =>
  "sym_lit"

  (:expr sn)
  # =>
  "(sym_lit)"

  (:eq rn (:parent (:parent sn)))
  # =>
  true

  (:start-byte sn)
  # =>
  1

  (:end-byte sn)
  # =>
  5

  (length (:text sn src))
  # =>
  4

  (= (- (:end-byte sn) (:start-byte sn))
     (length (:text sn src)))
  # =>
  true

  (:start-point sn)
  # =>
  [0 1]

  (:end-point sn)
  # =>
  [0 5]

  (:eq sn (:prev-sibling (:next-sibling sn)))
  # =>
  true

  (:is-named (:prev-sibling sn))
  # =>
  false

  (:text (:prev-sibling sn) src)
  # =>
  "("

  (def sqtn (:next-sibling (:next-sibling sn)))

  (:eq sqtn (:descendant-for-byte-range rn 12 14))
  # =>
  true

  (:eq sqtn (:descendant-for-point-range rn 0 12 0 14))
  # =>
  true

  (:type sqtn)
  # =>
  "sqr_tup_lit"

  (:expr sqtn)
  # =>
  "(sqr_tup_lit (sym_lit))"

  (:text (:descendant-for-byte-range rn 13 13) src)
  # =>
  "x"

  (:text (:descendant-for-point-range rn 0 13 0 13) src)
  # =>
  "x"

  )

# :parse can handle both buffers and strings
# XXX: somehow keywords and symbols work too, but that is unintentional
(comment

  (def lines
    @[@"(defn my-fn\n" # this is a buffer
      "  [x]\n"        # this is a string
      @"  (+ x 1))"])

  (def src
    (string/join lines ""))

  (def p (tree-sitter/init "janet_simple"))

  (assert p "Parser init failed")

  (def t (:parse p nil lines))

  (def rn (:root-node t))

  (:has-error rn)
  # =>
  false

  (:text rn src)
  # =>
  src

  )
