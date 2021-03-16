(import ../janet-tree-sitter/tree-sitter)

# replacing a character
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

  (:text rn src)
  # => src

  (:edit t
         8 9 9
         0 8
         0 9
         0 9)

  (def edited-lines
    @["(defn my+fn\n"
      "  [x]\n"
      "  (+ x 1))"])

  (def new-t
    (:parse p t edited-lines))

  (:text (:root-node new-t)
         (string/join edited-lines ""))
  # => "(defn my+fn\n  [x]\n  (+ x 1))"

  )

# deleting a character
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

  (:text rn src)
  # => src

  (:edit t
         8 9 9
         0 8
         0 9
         0 8)

  (def edited-lines
    @["(defn myfn\n"
      "  [x]\n"
      "  (+ x 1))"])

  (def new-t
    (:parse p t edited-lines))

  (:text (:root-node new-t)
         (string/join edited-lines ""))
  # => "(defn myfn\n  [x]\n  (+ x 1))"

  )

# inserting a character
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

  (:text rn src)
  # => src

  (:edit t
         8 9 10
         0 8
         0 9
         0 10)

  (def edited-lines
    @["(defn my+-fn\n"
      "  [x]\n"
      "  (+ x 1))"])

  (def new-t
    (:parse p t edited-lines))

  (:text (:root-node new-t)
         (string/join edited-lines ""))
  # => "(defn my+-fn\n  [x]\n  (+ x 1))"

  )

# replacing a line
(comment

  (def lines
    @[":a\n"
      ":b\n"
      ":c\n"])

  (def src
    (string/join lines ""))

  (def p (tree-sitter/init "janet_simple"))

  (def t (:parse-string p src))

  (def rn (:root-node t))

  (:text rn src)
  # => src

  (:edit t
         3 6 6
         1 0
         1 3
         1 3)

  (def edited-lines
    @[":a\n"
      ":x\n"
      ":c\n"])

  (def new-t
    (:parse p t edited-lines))

  (:text (:root-node new-t)
         (string/join edited-lines ""))
  # => ":a\n:x\n:c\n"

  )

# deleting a line
(comment

  (def lines
    @[":a\n"
      ":b\n"
      ":c\n"])

  (def src
    (string/join lines ""))

  (def p (tree-sitter/init "janet_simple"))

  (def t (:parse-string p src))

  (def rn (:root-node t))

  (:text rn src)
  # => src

  (:edit t
         3 6 3
         1 0
         1 3
         1 0)

  (def edited-lines
    @[":a\n"
      ":c\n"])

  (def new-t
    (:parse p t edited-lines))

  (:text (:root-node new-t)
         (string/join edited-lines ""))
  # => ":a\n:c\n"

  )

# inserting a line
(comment

  (def lines
    @[":a\n"
      ":b\n"
      ":c\n"])

  (def src
    (string/join lines ""))

  (def p (tree-sitter/init "janet_simple"))

  (def t (:parse-string p src))

  (def rn (:root-node t))

  (:text rn src)
  # => src

  (:edit t
         6 6 9
         2 0
         2 3
         4 0)

  (def edited-lines
    @[":a\n"
      ":b\n"
      ":x\n"
      ":c\n"])

  (def new-t
    (:parse p t edited-lines))

  (:text (:root-node new-t)
         (string/join edited-lines ""))
  # => ":a\n:b\n:x\n:c\n"

  )
