(import ../janet-tree-sitter/tree-sitter)

(comment

  # https://github.com/tree-sitter/tree-sitter-bash/issues/139
  (def src
    ``
    echo <<EOF
    text1 $var
    text2 $(echo cmd)
    EOF

    ``)

  (def p (tree-sitter/init "bash"))

  (def t (:parse-string p src))

  (def rn (:root-node t))

  (:has-error rn)
  # =>
  false

  (def here-doc-body
    (:child rn 2))

  (nil? (:first-child-for-byte here-doc-body 12))
  # =>
  false

  # XXX: should not be nil -- this is issue 139
  (nil? (:first-child-for-byte here-doc-body 22))
  # = >
  true

  (var nils @[])

  # last item being nil appears to be expected -- thus dec
  (for i 0 (dec (length src))
    (when (nil? (:first-child-for-byte rn i))
      (print "found nil at" i)
      (array/push nils i)))

  # no problems when using :first-child-for-byte for root node
  (empty? nils)
  # =>
  true

 )
