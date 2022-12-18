(import ../janet-tree-sitter/tree-sitter)

(comment

  (def lines
    @["(defn my-fn\n"
      "  [x]\n"
      "  (+ x 1))"])

  (def src
    (string/join lines ""))

  (def p (tree-sitter/init "janet_simple"))

  (def t (:parse-string p src))

  (def rn (:root-node t))

  (:text (:descendant-for-byte-range rn 0 0) src)
  # =>
  "("

  (:text (:descendant-for-byte-range rn 0 1) src)
  # =>
  "("

  (:text (:descendant-for-byte-range rn 14 14) src)
  # =>
  "["

  (:text (:descendant-for-byte-range rn 14 15) src)
  # =>
  "["

  (:text (:descendant-for-byte-range rn 14 16) src)
  # =>
  "[x]"

  (:text (:descendant-for-byte-range rn 14 17) src)
  # =>
  "[x]"
  
  (:text (:descendant-for-byte-range rn 13 17) src)
  # =>
  src

  )
