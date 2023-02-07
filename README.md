# janet-tree-sitter

Janet bindings for tree-sitter

## Prerequisites

* janet / jpm
* one or more relevant tree-sitter grammars installed locally

## Setup

```
git clone --recursive https://github.com/sogaiu/janet-tree-sitter
cd janet-tree-sitter
jpm install
```

## Usage Examples

### Basic

```janet
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
```

### Cursor

```janet
(def src "[:x :y :z]")

(def p (tree-sitter/init "clojure"))

(assert p "Parser init failed")

(def t (:parse-string p src))

(def rn (:root-node t))

(def c (tree-sitter/cursor rn))

(:expr (:node c))
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

(:text (:node c) src)
# =>
"["

(:go-parent c)
# =>
true

(:text (:node c) src)
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

(:text (:node c) src)
# =>
":z"

(:reset c rn)
# =>
nil

(:text (:node c) src)
# =>
"[:x :y :z]"
```

### Query

```janet
(def src "(def a 8)")

(def p (tree-sitter/init "clojure"))

(assert p "Parser init failed")

(def t (:parse-string p src))

(def rn (:root-node t))

(def q
  (tree-sitter/query "clojure" "_ @any"))

(def qc
  (tree-sitter/query-cursor))

(:exec qc q rn)
# =>
nil

(def [id patt_idx captures]
  (:next-match qc))

(def cap-node
  (get-in captures [0 1]))

(:eq rn cap-node)
# =>
true

(:next-match qc)

(:next-match qc)

(def [_ _ [[_ sym-node]]]
  (:next-match qc))

(:text sym-node src)
# =>
"def"

[(:start-byte sym-node) (:end-byte sym-node)]
# =>
[1 4]
```

## Issues

* To run all tests successfully, appropriate tree-sitter grammars for
  [clojure](https://github.com/sogaiu/tree-sitter-clojure) and
  [janet-simple](https://github.com/sogaiu/tree-sitter-janet-simple)
  need to be installed.

## Credits

* ahlinc - tree-sitter discussions, enhancements, and maintenance
* bakpakin - pieces of janet, path.janet
* damieng - tree-sitter discord server
* dannyfreeman - tree-sitter-clojure work and discussions
* maxbrunsfeld - tree-sitter
* pyrmont - discussion
* saikyun - discussion, text-experiment, and freja
