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

  (:has-error (:root-node new-t))
  # => false

  (:get-changed-ranges t new-t)
  # => nil

  (:text (:root-node new-t)
         (string/join edited-lines ""))
  # => "(defn my+fn\n  [x]\n  (+ x 1))"

  )

# deleting a character
(comment

  (def lines
    @["(:defn my-fn\n"
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
         1 2 1
         0 1
         0 2
         0 1)

  (def edited-lines
    @["(defn my-fn\n"
      "  [x]\n"
      "  (+ x 1))"])

  (def new-t
    (:parse p t edited-lines))

  (:has-error (:root-node new-t))
  # => false

  (:get-changed-ranges t new-t)
  # => '((1 5 0 1 0 5))

  (:text (:root-node new-t)
         (string/join edited-lines ""))
  # => "(defn my-fn\n  [x]\n  (+ x 1))"

  )

# inserting a character
(comment

  (def lines
    @["(defn my-fn \n"
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
         6 6 7
         0 6
         0 6
         0 7)

  (def edited-lines
    @["(defn :my-fn \n"
      "  [x]\n"
      "  (+ x 1))"])

  (def new-t
    (:parse p t edited-lines))

  (:has-error (:root-node new-t))
  # => false

  (:get-changed-ranges t new-t)
  # => '((6 12 0 6 0 12))

  (:text (:root-node new-t)
         (string/join edited-lines ""))
  # => "(defn :my-fn \n  [x]\n  (+ x 1))"

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

  (:has-error (:root-node new-t))
  # => false

  # XXX: keyword replaced a keyword, so why is this different?
  (:get-changed-ranges t new-t)
  # => '((5 6 1 2 1 3))

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

  (:has-error (:root-node new-t))
  # => false

  # XXX: removal is not leading to something being returned here...
  (:get-changed-ranges t new-t)
  # => nil

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

  (:has-error (:root-node new-t))
  # => false

  (:get-changed-ranges t new-t)
  # => '((6 8 2 0 2 2))

  (:text (:root-node new-t)
         (string/join edited-lines ""))
  # => ":a\n:b\n:x\n:c\n"

  )

# multiple edits
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
         1 1 2
         0 1
         0 1
         0 2)

  (def edited-lines-1
    @["(:defn my-fn\n"
      "  [x]\n"
      "  (+ x 1))"])

  (def new-t-1
    (:parse p t edited-lines-1))

  (:has-error (:root-node new-t-1))
  # => false

  (:get-changed-ranges t new-t-1)
  # => '((1 6 0 1 0 6))

  (:text (:root-node new-t-1)
         (string/join edited-lines-1 ""))
  # => "(:defn my-fn\n  [x]\n  (+ x 1))"

  (:edit new-t-1
         7 7 8
         0 7
         0 7
         0 8)

  (def edited-lines-2
    @["(:defn :my-fn\n"
      "  [x]\n"
      "  (+ x 1))"])

  (def new-t-2
    (:parse p new-t-1 edited-lines-2))

  (:has-error (:root-node new-t-2))
  # => false

  (:get-changed-ranges new-t-1 new-t-2)
  # => '((7 13 0 7 0 13))

  (:text (:root-node new-t-2)
         (string/join edited-lines-2 ""))
  # => "(:defn :my-fn\n  [x]\n  (+ x 1))"

  )

# single-line indentation
(comment

  (def lines
    @["(defn main\n"
      "[& args]\n"
      "1)"])

  (def src
    (string/join lines ""))

  (def p (tree-sitter/init "janet_simple"))

  (def t (:parse-string p src))

  (def rn (:root-node t))

  (:text rn src)
  # => src

  (:edit t
         11 11 13
         1 0
         1 0
         1 2)

  (def edited-lines
    @["(defn main\n"
      "  [& args]\n"
      "1)"])

  (def new-t
    (:parse p t edited-lines))

  (:has-error (:root-node new-t))
  # => false

  (:get-changed-ranges t new-t)
  # => nil

  (:text (:root-node new-t)
         (string/join edited-lines ""))
  # => "(defn main\n  [& args]\n1)"

)
