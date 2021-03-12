(declare-project
  :name "janet-tree-sitter"
  :description "Janet bindings for Tree-sitter"
  :url "https://gitlab.com/sogaiu/janet-tree-sitter"
  :repo "git+https://gitlab.com/sogaiu/janet-tree-sitter")

(declare-native
  :name "tree-sitter"
  :cflags   [;default-cflags
             "-Itree-sitter/lib/include"
             "-Itree-sitter/lib/src"
             "-std=c99" "-Wall" "-Wextra"
             # XXX: for debugging
             "-O0" "-g3"]
  :source ["janet-tree-sitter/tree_sitter.c"
           "tree-sitter/lib/src/lib.c"])
