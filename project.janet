(import ./support/path)

(def proj-name
  "janet-tree-sitter")

(def proj-root
  (os/cwd))

(def proj-dir-name
  proj-name)

(def src-root
  (path/join proj-root proj-dir-name))

(declare-project
  :name proj-name
  :description "Janet bindings for Tree-sitter"
  :url "https://gitlab.com/sogaiu/janet-tree-sitter"
  :repo "git+https://gitlab.com/sogaiu/janet-tree-sitter")

(declare-native
  :name "_tree-sitter"
  :cflags   [;default-cflags
             "-Itree-sitter/lib/include"
             "-Itree-sitter/lib/src"
             # XXX: for debugging with gdb
             #"-O0" "-g3"
            ]
  :source ["janet-tree-sitter/tree_sitter.c"
           "tree-sitter/lib/src/lib.c"])

(declare-source
  :source [proj-name])

# XXX: to get .so file and friends into janet-tree-sitter subdirectory:
#
#      1. add extra phony "build" bit to copy .so file and friends
#         to janet-tree-sitter subdirectory
#      2. modify the manifest file to remove mention of .so file and
#         friends (via "install" phony)
#      3. remove .so file and friends from JANET_MODPATH root
#         as they would be redundant (they also live in janet-tree-sitter
#         subdirectory) (via "install" phony

# XXX: factor out the install and build bits below into a file that
#      ends up living in vendor subdirectory

# XXX: depend on build?
(phony "install" []
       # tweak manifest file
       (def m (parse (slurp (find-manifest proj-name))))
       (def paths (filter (fn [path]
                            # XXX: good enough?
                            (not (string/has-suffix? proj-dir-name
                                                     path)))
                          (m :paths)))
       # XXX: more elegant way based on paths value possible?
       (def keep-paths (filter (fn [path]
                                 # XXX: good enough?
                                 (string/has-suffix? proj-dir-name
                                                     path))
                               (m :paths)))
       # modify manifest file
       (spit (find-manifest proj-name)
             (string/format "%j"
                            (table/to-struct
                             (put (table ;(kvs m)) :paths
                                (array ;keep-paths)))))
       # the following makes sense given our "build" phony target below
       # delete because these should be within proj-dir-name
       (each path paths
         (os/rm path)))

# copy necessary portions within build directory into janet-tree-sitter dir
(phony "build" []
       # find build/<relevant> items
       (var build-paths nil)
       (each entry (in (dyn :rules) "build")
         (each item entry
           (when (string? item)
             (when (or (string/has-prefix? "build/" item)
                       (string/has-prefix? "build\\" item))
               (set build-paths entry)
               (break)))))
       # XXX: possibly just ignore?
       (assert build-paths "failed to find build items")
       # copy build/<relevant> to proj-dir-name
       (each path build-paths
         (copy path proj-dir-name)))
