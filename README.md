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

## Usage Example

```janet
(import janet-tree-sitter/tree-sitter)

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
```

## Issues

* To run all tests successfully, appropriate tree-sitter grammars for
  clojure and janet-simple need to be installed.

## Credits

* bakpakin - pieces of janet, path.janet
* maxbrunsfeld - tree-sitter
* pyrmont - discussion
* Saikyun - discussion, text-experiment, and freja

