#include <janet.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <tree_sitter/api.h>

////////

// these bits are adapted from janet's source code

#if defined(WIN32) || defined(_WIN32)
#include <windows.h>
typedef HINSTANCE Clib;
#define load_clib(name) LoadLibrary((name))
#define symbol_clib(lib, sym) GetProcAddress((lib), (sym))
static char error_clib_buf[256];
static char *error_clib(void) {
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   NULL, GetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                   error_clib_buf, sizeof(error_clib_buf), NULL);
    error_clib_buf[strlen(error_clib_buf) - 1] = '\0';
    return error_clib_buf;
}
#else
#include <dlfcn.h>
typedef void *Clib;
#define load_clib(name) dlopen((name), RTLD_NOW)
#define symbol_clib(lib, sym) dlsym((lib), (sym))
#define error_clib() dlerror()
#endif

////////

typedef TSLanguage *(*JTSLang)(void);

////////

typedef struct {
    TSNode node;
} Node;

typedef struct {
    TSTree *tree;
} Tree;

typedef struct {
    TSParser *parser;
} Parser;

typedef struct {
    TSTreeCursor cursor;
} Cursor;

static int jts_node_get(void *p, Janet key, Janet *out);

const JanetAbstractType jts_node_type = {
    "tree-sitter/node",
    NULL,
    NULL,
    jts_node_get,
    JANET_ATEND_GET
};

static int jts_tree_gc(void *p, size_t size);

static int jts_tree_get(void *p, Janet key, Janet *out);

const JanetAbstractType jts_tree_type = {
    "tree-sitter/tree",
    jts_tree_gc,
    NULL,
    jts_tree_get,
    JANET_ATEND_GET
};

static int jts_parser_gc(void *p, size_t size);

static int jts_parser_get(void *p, Janet key, Janet *out);

const JanetAbstractType jts_parser_type = {
    "tree-sitter/parser",
    jts_parser_gc,
    NULL,
    jts_parser_get,
    JANET_ATEND_GET
};

static int jts_cursor_gc(void *p, size_t size);

static int jts_cursor_get(void *p, Janet key, Janet *out);

const JanetAbstractType jts_cursor_type = {
    "tree-sitter/cursor",
    jts_cursor_gc,
    NULL,
    jts_cursor_get,
    JANET_ATEND_GET
};

//////// start cfun_ts_init ////////

static Janet cfun_ts_init(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 2);

    const char *path = (const char *)janet_getstring(argv, 0);

    Clib lib = load_clib(path);
    if (!lib) {
        fprintf(stderr, "%s", error_clib());
        return janet_wrap_nil();
    }

    const char *fn_name = (const char *)janet_getstring(argv, 1);

    JTSLang jtsl;
    jtsl = (JTSLang) symbol_clib(lib, fn_name);
    if (!jtsl) {
        fprintf(stderr, "could not find the target grammar's initializer");
        return janet_wrap_nil();
    }

    TSParser *p = ts_parser_new();
    if (p == NULL) {
        fprintf(stderr, "ts_parser_new failed");
        return janet_wrap_nil();
    }

    Parser *parser =
        (Parser *)janet_abstract(&jts_parser_type, sizeof(Parser));
    parser->parser = p;

    bool success = ts_parser_set_language(p, jtsl());
    if (!success) {
        fprintf(stderr, "ts_parser_set_language failed");
        // XXX: abstract will take care of this?
        //free(p);
        return janet_wrap_nil();
    }

    return janet_wrap_abstract(parser);
}

//////// end cfun_ts_init ////////

/**
 * Get the node's type as a null-terminated string.
 */
static Janet cfun_node_type(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    Node *node = (Node *)janet_getabstract(argv, 0, &jts_node_type);
    // XXX: error checking?
    const char *the_type = ts_node_type(node->node);
    if (!the_type) {
        // XXX: is this appropriate handling?
        return janet_wrap_nil();
    }

    return janet_cstringv(the_type);
}

/**
 * Get the node's start byte.
 */
static Janet cfun_node_start_byte(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    // XXX: error checking?
    Node *node = janet_getabstract(argv, 0, &jts_node_type);
    return janet_wrap_integer(ts_node_start_byte(node->node));
}

/**
 * Get the node's end byte.
 */
static Janet cfun_node_end_byte(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    // XXX: error checking?
    Node *node = janet_getabstract(argv, 0, &jts_node_type);
    return janet_wrap_integer(ts_node_end_byte(node->node));
}

/**
 * Get the node's start position row and column.
 */
static Janet cfun_node_start_point(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    // XXX: error checking?
    Node *node = janet_getabstract(argv, 0, &jts_node_type);
    TSPoint point = ts_node_start_point(node->node);

    Janet *tup = janet_tuple_begin(2);
    tup[0] = janet_wrap_integer(point.row);
    tup[1] = janet_wrap_integer(point.column);

    return janet_wrap_tuple(janet_tuple_end(tup));
}

/**
 * Get the node's end position row and column.
 */
static Janet cfun_node_end_point(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    // XXX: error checking?
    Node *node = janet_getabstract(argv, 0, &jts_node_type);
    TSPoint point = ts_node_end_point(node->node);

    Janet *tup = janet_tuple_begin(2);
    tup[0] = janet_wrap_integer(point.row);
    tup[1] = janet_wrap_integer(point.column);

    return janet_wrap_tuple(janet_tuple_end(tup));
}

/**
 * Check if the node is null. Functions like `ts_node_child` and
 * `ts_node_next_sibling` will return a null node to indicate that no such node
 * was found.
 */
static Janet cfun_node_is_null(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    // XXX: error checking?
    Node *node = janet_getabstract(argv, 0, &jts_node_type);
    if (ts_node_is_null(node->node)) {
        return janet_wrap_true();
    } else {
        return janet_wrap_false();
    }
}

/**
 * Check if the node is *named*. Named nodes correspond to named rules in the
 * grammar, whereas *anonymous* nodes correspond to string literals in the
 * grammar.
 */
static Janet cfun_node_is_named(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    // XXX: error checking?
    Node *node = janet_getabstract(argv, 0, &jts_node_type);
    if (ts_node_is_named(node->node)) {
        return janet_wrap_true();
    } else {
        return janet_wrap_false();
    }
}

/**
 * Check if the node is a syntax error or contains any syntax errors.
 */
static Janet cfun_node_has_error(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    // XXX: error checking?
    Node *node = janet_getabstract(argv, 0, &jts_node_type);
    if (ts_node_has_error(node->node)) {
        return janet_wrap_true();
    } else {
        return janet_wrap_false();
    }
}

/**
 * Get the node's immediate parent.
 */
static Janet cfun_node_parent(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    // XXX: error checking?
    Node *node = janet_getabstract(argv, 0, &jts_node_type);
    Node *parent =
        (Node *)janet_abstract(&jts_node_type, sizeof(Node));
    // XXX: error checking?
    parent->node = ts_node_parent(node->node);
    if (ts_node_is_null(parent->node)) {
        return janet_wrap_nil();
    }
    return janet_wrap_abstract(parent);
}

/**
 * Get the node's child at the given index, where zero represents the first
 * child.
 */
static Janet cfun_node_child(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 2);
    // XXX: error checking?
    Node *node = janet_getabstract(argv, 0, &jts_node_type);
    // XXX: how to handle negative appropriately?
    uint32_t idx = (uint32_t)janet_getinteger(argv, 1);
    Node *child =
        (Node *)janet_abstract(&jts_node_type, sizeof(Node));
    // XXX: error checking?
    child->node = ts_node_child(node->node, idx);
    if (ts_node_is_null(child->node)) {
        return janet_wrap_nil();
    }
    return janet_wrap_abstract(child);
}

/**
 * Get the node's *named* child at the given index.
 */
static Janet cfun_node_named_child(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 2);
    // XXX: error checking?
    Node *node = janet_getabstract(argv, 0, &jts_node_type);
    // XXX: how to handle negative appropriately?
    uint32_t idx = (uint32_t)janet_getinteger(argv, 1);
    Node *child =
        (Node *)janet_abstract(&jts_node_type, sizeof(Node));
    // XXX: error checking?
    child->node = ts_node_named_child(node->node, idx);
    if (ts_node_is_null(child->node)) {
        return janet_wrap_nil();
    }
    return janet_wrap_abstract(child);
}

/**
 * Get the node's *named* child at the given index.
 */
static Janet cfun_node_child_count(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    // XXX: error checking?
    Node *node = janet_getabstract(argv, 0, &jts_node_type);
    // XXX: how to handle negative appropriately?
    return janet_wrap_integer(ts_node_child_count(node->node));
}

/**
 * Get the node's number of *named* children.
 */
static Janet cfun_node_named_child_count(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    // XXX: error checking?
    Node *node = janet_getabstract(argv, 0, &jts_node_type);
    // XXX: how to handle negative appropriately?
    return janet_wrap_integer(ts_node_named_child_count(node->node));
}

/**
 * Get the node's next sibling.
 */
static Janet cfun_node_next_sibling(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    // XXX: error checking?
    Node *node = janet_getabstract(argv, 0, &jts_node_type);
    Node *sibling =
        (Node *)janet_abstract(&jts_node_type, sizeof(Node));
    // XXX: error checking?
    sibling->node = ts_node_next_sibling(node->node);
    if (ts_node_is_null(sibling->node)) {
        return janet_wrap_nil();
    }
    return janet_wrap_abstract(sibling);
}

/**
 * Get the node's previous sibling.
 */
static Janet cfun_node_prev_sibling(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    // XXX: error checking?
    Node *node = janet_getabstract(argv, 0, &jts_node_type);
    Node *sibling =
        (Node *)janet_abstract(&jts_node_type, sizeof(Node));
    // XXX: error checking?
    sibling->node = ts_node_prev_sibling(node->node);
    if (ts_node_is_null(sibling->node)) {
        return janet_wrap_nil();
    }
    return janet_wrap_abstract(sibling);
}

/**
 * Get the smallest node within this node that spans the given range of bytes.
 */
static Janet cfun_node_descendant_for_byte_range(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 3);
    // XXX: error checking?
    Node *node = janet_getabstract(argv, 0, &jts_node_type);
    uint32_t start = (uint32_t)janet_getinteger(argv, 1);
    uint32_t end = (uint32_t)janet_getinteger(argv, 2);
    //
    Node *desc =
        (Node *)janet_abstract(&jts_node_type, sizeof(Node));
    // XXX: error checking?
    desc->node = ts_node_descendant_for_byte_range(node->node, start, end);
    if (ts_node_is_null(desc->node)) {
        return janet_wrap_nil();
    }
    return janet_wrap_abstract(desc);
}

/**
 * Get the smallest node within this node that spans the given range of
 * (row, column) positions.
 */
// XXX: not wrapping TSPoint
static Janet cfun_node_descendant_for_point_range(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 5);
    // XXX: error checking?
    Node *node = janet_getabstract(argv, 0, &jts_node_type);
    uint32_t start_row = (uint32_t)janet_getinteger(argv, 1);
    uint32_t start_col = (uint32_t)janet_getinteger(argv, 2);
    uint32_t end_row = (uint32_t)janet_getinteger(argv, 3);
    uint32_t end_col = (uint32_t)janet_getinteger(argv, 4);
    //
    TSPoint start_p = (TSPoint) {
        start_row, start_col
    };
    TSPoint end_p = (TSPoint) {
        end_row, end_col
    };
    //
    Node *desc =
        (Node *)janet_abstract(&jts_node_type, sizeof(Node));
    // XXX: error checking?
    desc->node = ts_node_descendant_for_point_range(node->node, start_p, end_p);
    if (ts_node_is_null(desc->node)) {
        return janet_wrap_nil();
    }
    return janet_wrap_abstract(desc);
}

/**
 * Get the node's first child that extends beyond the given byte offset.
 */
static Janet cfun_node_first_child_for_byte(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 2);
    // XXX: error checking?
    Node *node = janet_getabstract(argv, 0, &jts_node_type);
    // XXX: check for non-negative number?
    uint32_t idx = (uint32_t)janet_getinteger(argv, 1);
    Node *child =
        (Node *)janet_abstract(&jts_node_type, sizeof(Node));
    // XXX: error checking?
    child->node = ts_node_first_child_for_byte(node->node, idx);
    if (ts_node_is_null(child->node)) {
        return janet_wrap_nil();
    }
    return janet_wrap_abstract(child);
}

/**
 * Get the node's first named child that extends beyond the given byte offset.
 */
static Janet cfun_node_first_named_child_for_byte(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 2);
    // XXX: error checking?
    Node *node = janet_getabstract(argv, 0, &jts_node_type);
    // XXX: check for non-negative number?
    uint32_t idx = (uint32_t)janet_getinteger(argv, 1);
    Node *child =
        (Node *)janet_abstract(&jts_node_type, sizeof(Node));
    // XXX: error checking?
    child->node = ts_node_first_named_child_for_byte(node->node, idx);
    if (ts_node_is_null(child->node)) {
        return janet_wrap_nil();
    }
    return janet_wrap_abstract(child);
}

/**
 * Check if two nodes are identical.
 */
static Janet cfun_node_eq(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 2);
    // XXX: error checking?
    Node *node_l = janet_getabstract(argv, 0, &jts_node_type);
    // XXX: error checking?
    Node *node_r = janet_getabstract(argv, 1, &jts_node_type);
    if (ts_node_eq(node_l->node, node_r->node)) {
        return janet_wrap_true();
    } else {
        return janet_wrap_false();
    }
}

static Janet cfun_node_expr(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    // XXX: error checking?
    Node *node = janet_getabstract(argv, 0, &jts_node_type);
    char *text = ts_node_string(node->node);
    if (!text) {
        // XXX: is this appropriate handling?
        return janet_wrap_nil();
    }

    return janet_cstringv(text);
}

static Janet cfun_node_tree(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);

    Node *node = janet_getabstract(argv, 0, &jts_node_type);

    Tree *tree =
        (Tree *)janet_abstract(&jts_tree_type, sizeof(Tree));

    tree->tree = (node->node).tree;

    return janet_wrap_abstract(tree);
}

static Janet cfun_node_text(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 2);

    // XXX: error checking?
    Node *node = janet_getabstract(argv, 0, &jts_node_type);

    const char *source = (const char *)janet_getstring(argv, 1);
    if (!source) {
        // XXX: is this appropriate handling?
        return janet_wrap_nil();
    }

    uint32_t start = ts_node_start_byte(node->node);
    uint32_t end = ts_node_end_byte(node->node);

    size_t len = end - start;
    // XXX: should we be doing this copying?
    char *text = (char *)malloc(len + 1);
    if (NULL == text) {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }

    strncpy(text, source + start, len);
    text[len] = '\0';

    return janet_cstringv(text);
}

static const JanetMethod node_methods[] = {
    {"type", cfun_node_type},
    {"start-byte", cfun_node_start_byte},
    {"end-byte", cfun_node_end_byte},
    {"start-point", cfun_node_start_point},
    {"end-point", cfun_node_end_point},
    {"is-null", cfun_node_is_null},
    {"is-named", cfun_node_is_named},
    {"has-error", cfun_node_has_error},
    {"parent", cfun_node_parent},
    {"child", cfun_node_child},
    {"named-child", cfun_node_named_child},
    {"child-count", cfun_node_child_count},
    {"named-child-count", cfun_node_named_child_count},
    {"next-sibling", cfun_node_next_sibling},
    {"prev-sibling", cfun_node_prev_sibling},
    {"descendant-for-byte-range", cfun_node_descendant_for_byte_range},
    {"descendant-for-point-range", cfun_node_descendant_for_point_range},
    {"first-child-for-byte", cfun_node_first_child_for_byte},
    {"first-named-child-for-byte", cfun_node_first_named_child_for_byte},
    {"eq", cfun_node_eq},
    // custom
    {"expr", cfun_node_expr},
    {"tree", cfun_node_tree},
    {"text", cfun_node_text},
    {NULL, NULL}
};

int jts_node_get(void *p, Janet key, Janet *out) {
    (void) p;
    if (!janet_checktype(key, JANET_KEYWORD)) {
        return 0;
    }
    return janet_getmethod(janet_unwrap_keyword(key), node_methods, out);
}

////////

/**
 * Get the root node of the syntax tree.
 */
static Janet cfun_tree_root_node(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    // XXX: error checking?
    Tree *tree = janet_getabstract(argv, 0, &jts_tree_type);

    Node *rn =
        (Node *)janet_abstract(&jts_node_type, sizeof(Node));
    // XXX: error checking?

    rn->node = ts_tree_root_node(tree->tree);

    // XXX: is this appropriate checking?
    if (ts_node_is_null(rn->node)) {
        return janet_wrap_nil();
    }

    return janet_wrap_abstract(rn);
}

/**
 * Edit the syntax tree to keep it in sync with source code that has been
 * edited.
 *
 * You must describe the edit both in terms of byte offsets and in terms of
 * (row, column) coordinates.
 */
static Janet cfun_tree_edit(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 10);
    // XXX: error checking?
    Tree *tree = janet_getabstract(argv, 0, &jts_tree_type);
    uint32_t start_byte = janet_getinteger(argv, 1);
    uint32_t old_end_byte = janet_getinteger(argv, 2);
    uint32_t new_end_byte = janet_getinteger(argv, 3);
    TSPoint start_point = (TSPoint) {
        (uint32_t)janet_getinteger(argv, 4),
        (uint32_t)janet_getinteger(argv, 5)
    };
    TSPoint old_end_point = (TSPoint) {
        (uint32_t)janet_getinteger(argv, 6),
        (uint32_t)janet_getinteger(argv, 7)
    };
    TSPoint new_end_point = (TSPoint) {
        (uint32_t)janet_getinteger(argv, 8),
        (uint32_t)janet_getinteger(argv, 9)
    };

    TSInputEdit tsinputedit = (TSInputEdit) {
        .start_byte = start_byte,
        .old_end_byte = old_end_byte,
        .new_end_byte = new_end_byte,
        .start_point = start_point,
        .old_end_point = old_end_point,
        .new_end_point = new_end_point
    };

    ts_tree_edit(tree->tree, &tsinputedit);

    return janet_wrap_nil();
}

/**
 * Compare an old edited syntax tree to a new syntax tree representing the same
 * document, returning an array of ranges whose syntactic structure has changed.
 *
 * For this to work correctly, the old syntax tree must have been edited such
 * that its ranges match up to the new tree. Generally, you'll want to call
 * this function right after calling one of the `ts_parser_parse` functions.
 * You need to pass the old tree that was passed to parse, as well as the new
 * tree that was returned from that function.
 *
 * The returned array is allocated using `malloc` and the caller is responsible
 * for freeing it using `free`. The length of the array will be written to the
 * given `length` pointer.
 */
static Janet cfun_tree_get_changed_ranges(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 2);
    // XXX: error checking?
    Tree *old_tree = janet_getabstract(argv, 0, &jts_tree_type);
    Tree *new_tree = janet_getabstract(argv, 1, &jts_tree_type);
    uint32_t length = 0;

    TSRange *range =
        ts_tree_get_changed_ranges(old_tree->tree, new_tree->tree, &length);

    if (length == 0) {
        free(range);
        return janet_wrap_nil();
    }

    // XXX: hopefully this is an appropriate way to work with tuple-building
    Janet *ranges = janet_tuple_begin(length);

    for (int i = 0; i < length; i++) {
        Janet *tup = janet_tuple_begin(6);
        tup[0] = janet_wrap_integer(range[i].start_byte);
        tup[1] = janet_wrap_integer(range[i].end_byte);
        tup[2] = janet_wrap_integer(range[i].start_point.row);
        tup[3] = janet_wrap_integer(range[i].start_point.column);
        tup[4] = janet_wrap_integer(range[i].end_point.row);
        tup[5] = janet_wrap_integer(range[i].end_point.column);
        ranges[i] = janet_wrap_tuple(janet_tuple_end(tup));
    }

    free(range);

    return janet_wrap_tuple(janet_tuple_end(ranges));
}

static const JanetMethod tree_methods[] = {
    {"root-node", cfun_tree_root_node},
    {"edit", cfun_tree_edit},
    {"get-changed-ranges", cfun_tree_get_changed_ranges},
    {NULL, NULL}
};

static int jts_tree_gc(void *p, size_t size) {
    (void) size;
    Tree *tree = (Tree *)p;
    if (tree) {
        if (NULL != tree->tree) {
            ts_tree_delete(tree->tree);
            //free(tree->tree);
            tree->tree = NULL;
        }
    }
    return 0;
}

static int jts_tree_get(void *p, Janet key, Janet *out) {
    (void) p;
    if (!janet_checktype(key, JANET_KEYWORD)) {
        return 0;
    }
    return janet_getmethod(janet_unwrap_keyword(key), tree_methods, out);
}

////////

static Janet cfun_parser_delete(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    Parser *parser = janet_getabstract(argv, 0, &jts_parser_type);
    TSParser *tsparser = parser->parser;

    ts_parser_delete(tsparser);

    return janet_wrap_nil();
}

/* static Janet cfun_parser_set_language(int32_t argc, Janet* argv) { */

/* } */

static Janet cfun_parser_parse_string(int32_t argc, Janet *argv) {
    janet_arity(argc, 2, 3);
    Parser *parser = janet_getabstract(argv, 0, &jts_parser_type);
    TSParser *tsparser = parser->parser;
    TSTree *tstree = NULL;

    const char *src;
    if (argc == 2) {
        src = (const char *)janet_getstring(argv, 1);
    } else {
        Tree *old_tree = janet_getabstract(argv, 1, &jts_tree_type);
        tstree = old_tree->tree;
        src = (const char *)janet_getstring(argv, 2);
    }

    TSTree *new_tree = ts_parser_parse_string(
                           tsparser,
                           tstree,
                           src,
                           strlen(src)
                       );

    if (NULL == new_tree) {
        return janet_wrap_nil();
    }

    Tree *tree =
        (Tree *)janet_abstract(&jts_tree_type, sizeof(Tree));

    tree->tree = new_tree;

    return janet_wrap_abstract(tree);
}

static const char *jts_read_lines_fn(void *payload,
                                     uint32_t byte_index,
                                     TSPoint position,
                                     uint32_t *bytes_read) {
    JanetArray *lines = (JanetArray *)payload;

    uint32_t row = position.row;
    if (row >= lines->count) {
        *bytes_read = 0;
        // XXX: or should this be ""?
        return NULL;
    }

    const char *line;
    // XXX: keywords and symbols end up being handled by the JANET_STRING case
    if (janet_checktype(lines->data[row], JANET_BUFFER)) {
        JanetBuffer *buf = janet_unwrap_buffer(lines->data[row]);
        line = \
               (const char *)janet_string(buf->data, buf->count);
    } else if (janet_checktype(lines->data[row], JANET_STRING)) {
        line = (const char *)janet_unwrap_string(lines->data[row]);
    } else {
        // XXX: how to feedback error?
        janet_panicf("expected buffer or string, got something else");
    }

    uint32_t col = position.column;
    if (col >= strlen(line)) {
        *bytes_read = 0;
        // XXX: or should this be ""?
        return NULL;
    }

    *bytes_read = strlen(line) - col;
    // XXX: is this ok or is it necessary to somehow copy the data and return
    //      that instead?  how would one clean that up though?
    return line + col;
}

static Janet cfun_parser_parse(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 3);
    Parser *parser = janet_getabstract(argv, 0, &jts_parser_type);
    TSParser *tsparser = parser->parser;
    TSTree *tstree;
    Janet x = argv[1];
    if (janet_checktype(x, JANET_ABSTRACT)) {
        Tree *old_tree = janet_getabstract(argv, 1, &jts_tree_type);
        tstree = old_tree->tree;
    } else if (janet_checktype(x, JANET_NIL)) {
        tstree = NULL;
    } else {
        // XXX: how to feedback problem?
        return janet_wrap_nil();
    }

    JanetArray *lines = janet_getarray(argv, 2);

    TSInput tsinput = (TSInput) {
        .payload = (void *)lines,
        .read = &jts_read_lines_fn,
        .encoding = TSInputEncodingUTF8
    };

    TSTree *new_tree = ts_parser_parse(tsparser, tstree, tsinput);

    if (NULL == new_tree) {
        return janet_wrap_nil();
    }

    Tree *tree =
        (Tree *)janet_abstract(&jts_tree_type, sizeof(Tree));

    tree->tree = new_tree;

    return janet_wrap_abstract(tree);
}

// XXX: haven't implemented this general thing yet
/**
 * Set the logger that a parser should use during parsing.
 *
 * The parser does not take ownership over the logger payload. If a logger was
 * previously assigned, the caller is responsible for releasing any memory
 * owned by the previous logger.
 */
//void ts_parser_set_logger(TSParser *self, TSLogger logger);

void log_by_eprint(void *payload, TSLogType type, const char *message) {
    janet_eprintf("%s", message);
}

static Janet cfun_parser_log_by_eprint(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    Parser *parser = janet_getabstract(argv, 0, &jts_parser_type);
    TSParser *tsparser = parser->parser;

    TSLogger logger = {tsparser, log_by_eprint};

    ts_parser_set_logger(tsparser, logger);

    return janet_wrap_nil();
}

/**
 * Get the parser's current logger.
 */
//TSLogger ts_parser_logger(const TSParser *self);

/**
 * Set the file descriptor to which the parser should write debugging graphs
 * during parsing. The graphs are formatted in the DOT language. You may want
 * to pipe these graphs directly to a `dot(1)` process in order to generate
 * SVG output. You can turn off this logging by passing a negative number.
 */
//void ts_parser_print_dot_graphs(TSParser *self, int file);

static const JanetMethod parser_methods[] = {
    {"delete", cfun_parser_delete},
    //  {"set-language", cfun_parser_set_language},
    //  {"language", cfun_parser_language},
    {"parse-string", cfun_parser_parse_string},
    {"parse", cfun_parser_parse},
    //  {"set-included-ranges", cfun_parser_set_included_ranges},
    //  {"included-ranges", cfun_parser_included_ranges},
    //{"set-logger", cfun_parser_set_logger},
    {"log-by-eprint", cfun_parser_log_by_eprint},
    //{"logger", cfun_parser_logger},
    //{"print-dot-graphs", cfun_parser_print_dot_graphs},
    {NULL, NULL}
};

static int jts_parser_gc(void *p, size_t size) {
    (void) size;
    Parser *parser = (Parser *)p;
    if (parser) {
        if (NULL != parser->parser) {
            ts_parser_delete(parser->parser);
            //free(parser->parser);
            parser->parser = NULL;
        }
    }
    return 0;
}

static int jts_parser_get(void *p, Janet key, Janet *out) {
    (void) p;
    if (!janet_checktype(key, JANET_KEYWORD)) {
        return 0;
    }
    return janet_getmethod(janet_unwrap_keyword(key), parser_methods, out);
}

////////

/**
 * Create a new tree cursor starting from the given node.
 *
 * A tree cursor allows you to walk a syntax tree more efficiently than is
 * possible using the `TSNode` functions. It is a mutable object that is always
 * on a certain syntax node, and can be moved imperatively to different nodes.
 */
static Janet cfun_cursor_new(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);

    Node *node = janet_getabstract(argv, 0, &jts_node_type);
    if (ts_node_is_null(node->node)) {
        fprintf(stderr, "node was null");
        return janet_wrap_nil();
    }

    TSTreeCursor c = ts_tree_cursor_new(node->node);
    // XXX: can't fail?

    Cursor *cursor =
        (Cursor *)janet_abstract(&jts_cursor_type, sizeof(Cursor));
    cursor->cursor = c;

    return janet_wrap_abstract(cursor);
}

/**
 * Move the cursor to the parent of its current node.
 *
 * This returns `true` if the cursor successfully moved, and returns `false`
 * if there was no parent node (the cursor was already on the root node).
 */
static Janet cfun_cursor_goto_parent(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);

    Cursor *cursor = janet_getabstract(argv, 0, &jts_cursor_type);
    // XXX: error-checking?

    if (ts_tree_cursor_goto_parent(&(cursor->cursor))) {
      return janet_wrap_true();
    } else {
      return janet_wrap_false();
    }
}

/**
 * Move the cursor to the next sibling of its current node.
 *
 * This returns `true` if the cursor successfully moved, and returns `false`
 * if there was no next sibling node.
 */
static Janet cfun_cursor_goto_next_sibling(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);

    Cursor *cursor = janet_getabstract(argv, 0, &jts_cursor_type);
    // XXX: error-checking?

    if (ts_tree_cursor_goto_next_sibling(&(cursor->cursor))) {
      return janet_wrap_true();
    } else {
      return janet_wrap_false();
    }
}

/**
 * Move the cursor to the first child of its current node.
 *
 * This returns `true` if the cursor successfully moved, and returns `false`
 * if there were no children.
 */
static Janet cfun_cursor_goto_first_child(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);

    Cursor *cursor = janet_getabstract(argv, 0, &jts_cursor_type);
    // XXX: error-checking?

    if (ts_tree_cursor_goto_first_child(&(cursor->cursor))) {
      return janet_wrap_true();
    } else {
      return janet_wrap_false();
    }
}

/**
 * Re-initialize a tree cursor to start at a different node.
 */
static Janet cfun_cursor_reset(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 2);

    Cursor *cursor = janet_getabstract(argv, 0, &jts_cursor_type);
    // XXX: error-checking?

    Node *node = janet_getabstract(argv, 1, &jts_node_type);
    if (ts_node_is_null(node->node)) {
        fprintf(stderr, "node was null");
        return janet_wrap_nil();
    }

    ts_tree_cursor_reset(&(cursor->cursor), node->node);

    // XXX: better to return true?
    return janet_wrap_nil();
}

/**
 * Get the tree cursor's current node.
 */
static Janet cfun_cursor_current_node(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);

    Cursor *cursor = janet_getabstract(argv, 0, &jts_cursor_type);
    // XXX: error-checking?

    Node *n =
        (Node *)janet_abstract(&jts_node_type, sizeof(Node));
    // XXX: error checking?

    n->node = ts_tree_cursor_current_node(&(cursor->cursor));

    // XXX: is this appropriate checking?
    if (ts_node_is_null(n->node)) {
        return janet_wrap_nil();
    }

    return janet_wrap_abstract(n);
}

/**
 * Get the field name of the tree cursor's current node.
 *
 * This returns `NULL` if the current node doesn't have a field.
 * See also `ts_node_child_by_field_name`.
 */
static Janet cfun_cursor_current_field_name(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);

    Cursor *cursor = janet_getabstract(argv, 0, &jts_cursor_type);
    // XXX: error-checking?
    const char *name = ts_tree_cursor_current_field_name(&(cursor->cursor));
    if (!name) {
        // XXX: is this appropriate handling?
        return janet_wrap_nil();
    }

    return janet_cstringv(name);
}

static const JanetMethod cursor_methods[] = {
    {"go-parent", cfun_cursor_goto_parent},
    {"go-next-sibling", cfun_cursor_goto_next_sibling},
    {"go-first-child", cfun_cursor_goto_first_child},
    {"reset", cfun_cursor_reset},
    {"node", cfun_cursor_current_node},
    {"field-name", cfun_cursor_current_field_name},
    {NULL, NULL}
};

static int jts_cursor_gc(void *p, size_t size) {
    (void) size;
    Cursor *cursor = (Cursor *)p;
    if (cursor) {
        if (NULL != &(cursor->cursor)) {
            ts_tree_cursor_delete(&(cursor->cursor));
            // XXX: ?
            //&(cursor->cursor) = NULL;
        }
    }
    return 0;
}

static int jts_cursor_get(void *p, Janet key, Janet *out) {
    (void) p;
    if (!janet_checktype(key, JANET_KEYWORD)) {
        return 0;
    }
    return janet_getmethod(janet_unwrap_keyword(key), cursor_methods, out);
}

////////

static const JanetReg cfuns[] = {
    {
        "_init", cfun_ts_init,
        "(_tree-sitter/_init path fn-name)\n\n"
        "Return tree-sitter parser for grammar.\n"
        "`path` is a file path to the dynamic library for a grammar.\n"
        "`fn-name` is the grammar init function name as a string, e.g.\n"
        "`tree_sitter_clojure` or `tree_sitter_janet_simple`."
    },
    {
        "_cursor", cfun_cursor_new,
        "(_tree-sitter/_cursor node)\n\n"
        "Return new cursor for node.\n"
    },
    {NULL, NULL, NULL}
};

JANET_MODULE_ENTRY(JanetTable *env) {
    janet_register_abstract_type(&jts_cursor_type);
    janet_register_abstract_type(&jts_parser_type);
    janet_register_abstract_type(&jts_tree_type);
    janet_register_abstract_type(&jts_node_type);
    janet_cfuns(env, "tree-sitter", cfuns);
}

