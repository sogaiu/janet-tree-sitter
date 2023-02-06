#include <janet.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <tree_sitter/api.h>

// XXX: start adaptaion from parser.c

#include "clock.h"
#include "get_changed_ranges.h"
#include "lexer.h"
#include "reduce_action.h"
#include "reusable_node.h"
#include "stack.h"
#include "subtree.h"

typedef struct {
  Subtree token;
  Subtree last_external_token;
  uint32_t byte_index;
} TokenCache;

struct TSParser {
  Lexer lexer;
  Stack *stack;
  SubtreePool tree_pool;
  const TSLanguage *language;
  ReduceActionSet reduce_actions;
  Subtree finished_tree;
  SubtreeArray trailing_extras;
  SubtreeArray trailing_extras2;
  SubtreeArray scratch_trees;
  TokenCache token_cache;
  ReusableNode reusable_node;
  void *external_scanner_payload;
  FILE *dot_graph_file;
  TSClock end_clock;
  TSDuration timeout_duration;
  unsigned accept_count;
  unsigned operation_count;
  const volatile size_t *cancellation_flag;
  Subtree old_tree;
  TSRangeArray included_range_differences;
  unsigned included_range_difference_index;
};

// XXX: end adaptation from parser.c

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
  TSTree *tree;
} Tree;

typedef struct {
  TSParser *parser;
} Parser;

typedef struct {
  TSTreeCursor cursor;
} Cursor;

typedef struct {
  TSLanguage *language;
} Language;

typedef struct {
  TSQuery *query;
} Query;

typedef struct {
  TSQueryCursor *query_cursor;
} QueryCursor;

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

const JanetAbstractType jts_language_type = {
  "tree-sitter/language",
  JANET_ATEND_GET
};

static int jts_query_gc(void *p, size_t size);

static int jts_query_get(void *p, Janet key, Janet *out);

const JanetAbstractType jts_query_type = {
  "tree-sitter/query",
  jts_query_gc,
  NULL,
  jts_query_get,
  JANET_ATEND_GET
};

static int jts_query_cursor_gc(void *p, size_t size);

static int jts_query_cursor_get(void *p, Janet key, Janet *out);

const JanetAbstractType jts_query_cursor_type = {
  "tree-sitter/query_cursor",
  jts_query_cursor_gc,
  NULL,
  jts_query_cursor_get,
  JANET_ATEND_GET
};

//////// start cfun_ts_init ////////

static Janet cfun_ts_init(int32_t argc, Janet *argv) {
  janet_fixarity(argc, 2);

  const char *path = (const char *)janet_getstring(argv, 0);
  if (!path) {
    return janet_wrap_nil();
  }

  Clib lib = load_clib(path);
  if (!lib) {
    fprintf(stderr, "%s", error_clib());
    return janet_wrap_nil();
  }

  const char *fn_name = (const char *)janet_getstring(argv, 1);
  if (!fn_name) {
    return janet_wrap_nil();
  }

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
    return janet_wrap_nil();
  }

  return janet_wrap_abstract(parser);
}

//////// end cfun_ts_init ////////

static TSNode *jts_get_node(const Janet *argv, int32_t n) {
  return (TSNode *)janet_getabstract(argv, n, &jts_node_type);
}

/**
 * Get the node's type as a null-terminated string.
 */
static Janet cfun_node_type(int32_t argc, Janet *argv) {
  janet_fixarity(argc, 1);

  TSNode node = *jts_get_node(argv, 0);

  const char *the_type = ts_node_type(node);
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

  TSNode node = *jts_get_node(argv, 0);
  if (ts_node_is_null(node)) {
    return janet_wrap_nil();
  }

  return janet_wrap_integer(ts_node_start_byte(node));
}

/**
 * Get the node's end byte.
 */
static Janet cfun_node_end_byte(int32_t argc, Janet *argv) {
  janet_fixarity(argc, 1);

  TSNode node = *jts_get_node(argv, 0);
  if (ts_node_is_null(node)) {
    return janet_wrap_nil();
  }

  return janet_wrap_integer(ts_node_end_byte(node));
}

/**
 * Get the node's start position row and column.
 */
static Janet cfun_node_start_point(int32_t argc, Janet *argv) {
  janet_fixarity(argc, 1);

  TSNode node = *jts_get_node(argv, 0);
  if (ts_node_is_null(node)) {
    return janet_wrap_nil();
  }

  TSPoint point = ts_node_start_point(node);

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

  TSNode node = *jts_get_node(argv, 0);
  if (ts_node_is_null(node)) {
    return janet_wrap_nil();
  }

  TSPoint point = ts_node_end_point(node);

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

  TSNode node = *jts_get_node(argv, 0);
  if (ts_node_is_null(node)) {
    return janet_wrap_nil();
  }

  if (ts_node_is_null(node)) {
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

  TSNode node = *jts_get_node(argv, 0);
  if (ts_node_is_null(node)) {
    return janet_wrap_nil();
  }

  if (ts_node_is_named(node)) {
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

  TSNode node = *jts_get_node(argv, 0);
  if (ts_node_is_null(node)) {
    return janet_wrap_nil();
  }

  if (ts_node_has_error(node)) {
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

  TSNode node = *jts_get_node(argv, 0);
  if (ts_node_is_null(node)) {
    return janet_wrap_nil();
  }

  TSNode *parent_p =
    (TSNode *)janet_abstract(&jts_node_type, sizeof(TSNode));

  *parent_p = ts_node_parent(node);
  if (ts_node_is_null(*parent_p)) {
    return janet_wrap_nil();
  }

  return janet_wrap_abstract(parent_p);
}

/**
 * Get the node's child at the given index, where zero represents the first
 * child.
 */
static Janet cfun_node_child(int32_t argc, Janet *argv) {
  janet_fixarity(argc, 2);

  TSNode node = *jts_get_node(argv, 0);
  if (ts_node_is_null(node)) {
    return janet_wrap_nil();
  }

  // XXX: how to handle negative appropriately?
  uint32_t idx = (uint32_t)janet_getinteger(argv, 1);

  TSNode *child_p =
    (TSNode *)janet_abstract(&jts_node_type, sizeof(TSNode));

  *child_p = ts_node_child(node, idx);
  if (ts_node_is_null(*child_p)) {
    return janet_wrap_nil();
  }

  return janet_wrap_abstract(child_p);
}

/**
 * Get the node's *named* child at the given index.
 */
static Janet cfun_node_named_child(int32_t argc, Janet *argv) {
  janet_fixarity(argc, 2);

  TSNode node = *jts_get_node(argv, 0);
  if (ts_node_is_null(node)) {
    return janet_wrap_nil();
  }

  // XXX: how to handle negative appropriately?
  uint32_t idx = (uint32_t)janet_getinteger(argv, 1);

  TSNode *child_p =
    (TSNode *)janet_abstract(&jts_node_type, sizeof(TSNode));
  // XXX: error checking?
  *child_p = ts_node_named_child(node, idx);
  if (ts_node_is_null(*child_p)) {
    return janet_wrap_nil();
  }

  return janet_wrap_abstract(child_p);
}

/**
 * Get the node's *named* child at the given index.
 */
static Janet cfun_node_child_count(int32_t argc, Janet *argv) {
  janet_fixarity(argc, 1);

  TSNode node = *jts_get_node(argv, 0);
  if (ts_node_is_null(node)) {
    return janet_wrap_nil();
  }

  // XXX: how to handle negative appropriately?
  return janet_wrap_integer(ts_node_child_count(node));
}

/**
 * Get the node's number of *named* children.
 */
static Janet cfun_node_named_child_count(int32_t argc, Janet *argv) {
  janet_fixarity(argc, 1);

  TSNode node = *jts_get_node(argv, 0);
  if (ts_node_is_null(node)) {
    return janet_wrap_nil();
  }

  // XXX: how to handle negative appropriately?
  return janet_wrap_integer(ts_node_named_child_count(node));
}

/**
 * Get the node's next sibling.
 */
static Janet cfun_node_next_sibling(int32_t argc, Janet *argv) {
  janet_fixarity(argc, 1);

  TSNode node = *jts_get_node(argv, 0);
  if (ts_node_is_null(node)) {
    return janet_wrap_nil();
  }

  TSNode *sibling_p =
    (TSNode *)janet_abstract(&jts_node_type, sizeof(TSNode));

  *sibling_p = ts_node_next_sibling(node);
  if (ts_node_is_null(*sibling_p)) {
    return janet_wrap_nil();
  }

  return janet_wrap_abstract(sibling_p);
}

/**
 * Get the node's previous sibling.
 */
static Janet cfun_node_prev_sibling(int32_t argc, Janet *argv) {
  janet_fixarity(argc, 1);

  TSNode node = *jts_get_node(argv, 0);
  if (ts_node_is_null(node)) {
    return janet_wrap_nil();
  }

  TSNode *sibling_p =
    (TSNode *)janet_abstract(&jts_node_type, sizeof(TSNode));

  *sibling_p = ts_node_prev_sibling(node);
  if (ts_node_is_null(*sibling_p)) {
    return janet_wrap_nil();
  }

  return janet_wrap_abstract(sibling_p);
}

/**
 * Get the smallest node within this node that spans the given range of bytes.
 */
static Janet cfun_node_descendant_for_byte_range(int32_t argc, Janet *argv) {
  janet_fixarity(argc, 3);

  TSNode node = *jts_get_node(argv, 0);
  if (ts_node_is_null(node)) {
    return janet_wrap_nil();
  }

  uint32_t start = (uint32_t)janet_getinteger(argv, 1);
  uint32_t end = (uint32_t)janet_getinteger(argv, 2);

  TSNode *desc_p =
    (TSNode *)janet_abstract(&jts_node_type, sizeof(TSNode));

  *desc_p = ts_node_descendant_for_byte_range(node, start, end);
  if (ts_node_is_null(*desc_p)) {
    return janet_wrap_nil();
  }

  return janet_wrap_abstract(desc_p);
}

/**
 * Get the smallest node within this node that spans the given range of
 * (row, column) positions.
 */
// XXX: not wrapping TSPoint
static Janet cfun_node_descendant_for_point_range(int32_t argc, Janet *argv) {
  janet_fixarity(argc, 5);

  TSNode node = *jts_get_node(argv, 0);
  if (ts_node_is_null(node)) {
    return janet_wrap_nil();
  }

  uint32_t start_row = (uint32_t)janet_getinteger(argv, 1);
  uint32_t start_col = (uint32_t)janet_getinteger(argv, 2);
  uint32_t end_row = (uint32_t)janet_getinteger(argv, 3);
  uint32_t end_col = (uint32_t)janet_getinteger(argv, 4);

  TSPoint start_point = (TSPoint) {
    start_row, start_col
  };

  TSPoint end_point = (TSPoint) {
    end_row, end_col
  };

  TSNode *desc_p =
    (TSNode *)janet_abstract(&jts_node_type, sizeof(TSNode));

  *desc_p = ts_node_descendant_for_point_range(node, start_point, end_point);
  if (ts_node_is_null(*desc_p)) {
    return janet_wrap_nil();
  }

  return janet_wrap_abstract(desc_p);
}

/**
 * Get the node's first child that extends beyond the given byte offset.
 */
static Janet cfun_node_first_child_for_byte(int32_t argc, Janet *argv) {
  janet_fixarity(argc, 2);

  TSNode node = *jts_get_node(argv, 0);
  if (ts_node_is_null(node)) {
    return janet_wrap_nil();
  }

  // XXX: check for non-negative number?
  uint32_t idx = (uint32_t)janet_getinteger(argv, 1);

  TSNode *child_p =
    (TSNode *)janet_abstract(&jts_node_type, sizeof(TSNode));

  *child_p = ts_node_first_child_for_byte(node, idx);
  if (ts_node_is_null(*child_p)) {
    return janet_wrap_nil();
  }

  return janet_wrap_abstract(child_p);
}

/**
 * Get the node's first named child that extends beyond the given byte offset.
 */
static Janet cfun_node_first_named_child_for_byte(int32_t argc, Janet *argv) {
  janet_fixarity(argc, 2);

  TSNode node = *jts_get_node(argv, 0);
  if (ts_node_is_null(node)) {
    return janet_wrap_nil();
  }

  // XXX: check for non-negative number?
  uint32_t idx = (uint32_t)janet_getinteger(argv, 1);

  TSNode *child_p =
    (TSNode *)janet_abstract(&jts_node_type, sizeof(TSNode));

  *child_p = ts_node_first_named_child_for_byte(node, idx);
  if (ts_node_is_null(*child_p)) {
    return janet_wrap_nil();
  }

  return janet_wrap_abstract(child_p);
}

/**
 * Check if two nodes are identical.
 */
static Janet cfun_node_eq(int32_t argc, Janet *argv) {
  janet_fixarity(argc, 2);

  TSNode node_l = *jts_get_node(argv, 0);
  if (ts_node_is_null(node_l)) {
    return janet_wrap_nil();
  }

  TSNode node_r = *jts_get_node(argv, 1);
  if (ts_node_is_null(node_r)) {
    return janet_wrap_nil();
  }

  if (ts_node_eq(node_l, node_r)) {
    return janet_wrap_true();
  } else {
    return janet_wrap_false();
  }
}

static Janet cfun_node_expr(int32_t argc, Janet *argv) {
  janet_fixarity(argc, 1);

  TSNode node = *jts_get_node(argv, 0);
  if (ts_node_is_null(node)) {
    return janet_wrap_nil();
  }

  char *text = ts_node_string(node);
  if (!text) {
    return janet_wrap_nil();
  }

  return janet_cstringv(text);
}

static Janet cfun_node_tree(int32_t argc, Janet *argv) {
  janet_fixarity(argc, 1);

  TSNode node = *jts_get_node(argv, 0);
  if (ts_node_is_null(node)) {
    return janet_wrap_nil();
  }

  Tree *tree =
    (Tree *)janet_abstract(&jts_tree_type, sizeof(Tree));

  tree->tree = node.tree;

  return janet_wrap_abstract(tree);
}

static Janet cfun_node_text(int32_t argc, Janet *argv) {
  janet_fixarity(argc, 2);

  TSNode node = *jts_get_node(argv, 0);
  if (ts_node_is_null(node)) {
    return janet_wrap_nil();
  }

  const char *source = (const char *)janet_getstring(argv, 1);
  if (!source) {
    return janet_wrap_nil();
  }

  uint32_t start = ts_node_start_byte(node);
  uint32_t end = ts_node_end_byte(node);

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

  TSNode *node_p =
    (TSNode *)janet_abstract(&jts_node_type, sizeof(TSNode));

  *node_p = ts_tree_root_node(tree->tree);
  if (ts_node_is_null(*node_p)) {
    return janet_wrap_nil();
  }

  return janet_wrap_abstract(node_p);
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

/**
 * Write a DOT graph describing the syntax tree to the given file.
 */
static Janet cfun_tree_print_dot_graph(int32_t argc, Janet *argv) {
  janet_fixarity(argc, 2);
  // XXX: error checking?
  Tree *tree = janet_getabstract(argv, 0, &jts_tree_type);

  // XXX: is this safe?
  JanetFile *of =
    (JanetFile *)janet_getabstract(argv, 1, &janet_file_type);

  // XXX: check of->flags to make sure writable?
  //      what about appened, etc.?
  if (!(of->flags & JANET_FILE_WRITE)) {
    return janet_wrap_nil();
  }

  ts_tree_print_dot_graph(tree->tree, of->file);

  return janet_wrap_nil();
}

//void ts_tree_print_dot_graph(const TSTree *, FILE *);

static const JanetMethod tree_methods[] = {
  {"root-node", cfun_tree_root_node},
  {"edit", cfun_tree_edit},
  {"get-changed-ranges", cfun_tree_get_changed_ranges},
  {"print-dot-graph", cfun_tree_print_dot_graph},
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

/**
 * Get the parser's current language.
 */
static Janet cfun_parser_language(int32_t argc, Janet *argv) {
  janet_fixarity(argc, 1);
  Parser *parser = janet_getabstract(argv, 0, &jts_parser_type);
  TSParser *tsparser = parser->parser;

  Language *language =
    (Language *)janet_abstract(&jts_language_type, sizeof(Language));

  language->language = ts_parser_language(tsparser);

  // XXX: appropriate check?
  if (!(language->language)) {
    return janet_wrap_nil();
  }

  return janet_wrap_abstract(language);
}

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

  TSTree *new_tree = ts_parser_parse_string(tsparser,
                                            tstree,
                                            src,
                                            strlen(src));

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
  if (type == TSLogTypeLex) {
    janet_eprintf("  %s\n", message);
  } else {
    janet_eprintf("%s\n", message);
  }
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
static Janet cfun_parser_print_dot_graphs_0(int32_t argc, Janet *argv) {
  janet_fixarity(argc, 2);
  Parser *parser = janet_getabstract(argv, 0, &jts_parser_type);
  TSParser *tsparser = parser->parser;

  // XXX: is this safe?
  JanetFile *of =
    (JanetFile *)janet_getabstract(argv, 1, &janet_file_type);

  if (!of) {
    return janet_wrap_nil();
  }

  // XXX: check of->flags to make sure writable?
  //      what about appened, etc.?
  if (!(of->flags & JANET_FILE_WRITE)) {
    return janet_wrap_nil();
  }

  // XXX: britle?  cumbersome to get working?  ended up
  //      including portions of parser.c to get this to work.
  //      may be there's a better way?
  tsparser->dot_graph_file = of->file;

  // XXX: more useful than nil as a return value?
  return janet_wrap_true();
}
//void ts_parser_print_dot_graphs(TSParser *self, int file);

static const JanetMethod parser_methods[] = {
  //{"delete", cfun_parser_delete},
  //{"set-language", cfun_parser_set_language},
  {"language", cfun_parser_language},
  {"parse-string", cfun_parser_parse_string},
  {"parse", cfun_parser_parse},
  //{"set-included-ranges", cfun_parser_set_included_ranges},
  //{"included-ranges", cfun_parser_included_ranges},
  //{"set-logger", cfun_parser_set_logger},
  //{"logger", cfun_parser_logger},
  //{"print-dot-graphs", cfun_parser_print_dot_graphs},
  {"print-dot-graphs-0", cfun_parser_print_dot_graphs_0},
  // custom
  {"log-by-eprint", cfun_parser_log_by_eprint},
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

  TSNode node = *jts_get_node(argv, 0);
  if (ts_node_is_null(node)) {
    return janet_wrap_nil();
  }

  TSTreeCursor c = ts_tree_cursor_new(node);
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

  TSNode node = *jts_get_node(argv, 1);
  if (ts_node_is_null(node)) {
    return janet_wrap_nil();
  }

  ts_tree_cursor_reset(&(cursor->cursor), node);

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

  TSNode *node_p =
    (TSNode *)janet_abstract(&jts_node_type, sizeof(TSNode));

  *node_p = ts_tree_cursor_current_node(&(cursor->cursor));
  if (ts_node_is_null(*node_p)) {
    return janet_wrap_nil();
  }

  return janet_wrap_abstract(node_p);
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

/**
 * Create a new query from a string containing one or more S-expression
 * patterns. The query is associated with a particular language, and can
 * only be run on syntax nodes parsed with that language.
 *
 * If all of the given patterns are valid, this returns a `TSQuery`.
 * If a pattern is invalid, this returns `NULL`, and provides two pieces
 * of information about the problem:
 * 1. The byte offset of the error is written to the `error_offset` parameter.
 * 2. The type of error is written to the `error_type` parameter.
 */
static Janet cfun_query_new(int32_t argc, Janet *argv) {
  janet_fixarity(argc, 2);

  Language *language = janet_getabstract(argv, 0, &jts_language_type);
  TSLanguage *tslang = language->language;

  const char *src = (const char *)janet_getstring(argv, 1);

  // XXX: is this off by one?
  uint32_t src_len = (uint32_t)strlen(src);

  uint32_t error_offset;
  TSQueryError error_type;

  TSQuery *tsquery =
    ts_query_new(tslang, src, src_len, &error_offset, &error_type);

  if (!tsquery) {
    Janet *tup = janet_tuple_begin(2);
    // XXX: might lose info?
    tup[0] = janet_wrap_integer((int32_t)(error_offset));
    switch (error_type) {
      default:
        fprintf(stderr,
                "Unexpected TSQueryError: %d\n",
                (int)(error_type));
        exit(1);
        break;
      case TSQueryErrorNone:
        tup[1] = janet_ckeywordv("none");
        break;
      case TSQueryErrorSyntax:
        tup[1] = janet_ckeywordv("syntax");
        break;
      case TSQueryErrorNodeType:
        tup[1] = janet_ckeywordv("node-type");
        break;
      case TSQueryErrorField:
        tup[1] = janet_ckeywordv("field");
        break;
      case TSQueryErrorCapture:
        tup[1] = janet_ckeywordv("capture");
        break;
    }
    return janet_wrap_tuple(janet_tuple_end(tup));
  }

  Query *q =
    (Query *)janet_abstract(&jts_query_type, sizeof(Query));

  q->query = tsquery;

  return janet_wrap_abstract(q);
}

/*
  TSQuery *ts_query_new(
  const TSLanguage *language,
  const char *source,
  uint32_t source_len,
  uint32_t *error_offset,
  TSQueryError *error_type
  );
*/

/**
 * Get the name and length of one of the query's captures, or one of the
 * query's string literals. Each capture and string is associated with a
 * numeric id based on the order that it appeared in the query's source.
 */
static Janet cfun_query_capture_name_for_id(int32_t argc, Janet *argv) {
  janet_fixarity(argc, 2);

  Query *query = janet_getabstract(argv, 0, &jts_query_type);
  // XXX; error-checking?

  uint32_t id = (uint32_t)janet_getinteger(argv, 1);

  uint32_t length;

  const char *name =
    ts_query_capture_name_for_id(query->query, id, &length);

  if (!name) {
    return janet_wrap_nil();
  }

  Janet *tup = janet_tuple_begin(2);

  tup[0] = janet_cstringv(name);
  tup[1] = janet_wrap_integer(length);

  return janet_wrap_tuple(janet_tuple_end(tup));
}

/*
  const char *ts_query_capture_name_for_id(
  const TSQuery *,
  uint32_t id,
  uint32_t *length
  );
*/

static const JanetMethod query_methods[] = {
  /*
    {"pattern-count", cfun_query_pattern_count},
    {"capture-count", cfun_query_capture_count},
    {"string-count", cfun_query_string_count},
    {"start-byte-for-pattern", cfun_query_start_byte_for_pattern},
    {"predicates-for-pattern", cfun_query_predicates_for_pattern},
    {"is-pattern-rooted", cfun_query_is_pattern_rooted},
    {"is-pattern-guaranteed-at-step", cfun_query_is_pattern_guaranteed_at_step},
  */
  {"capture-name-for-id", cfun_query_capture_name_for_id},
  /*
    {"capture-quantifier-for-id", cfun_query_capture_quantifier_for_id},
    {"string-value-for-id", cfun_query_string_value_for_id},
    {"disable-capture", cfun_query_disable_capture},
    {"disable-pattern", cfun_query_disable_pattern},
  */
  {NULL, NULL}
};

static int jts_query_gc(void *p, size_t size) {
  (void) size;

  Query *query = (Query *)p;
  if (query) {
    if (NULL != query->query) {
      ts_query_delete(query->query);
      // XXX: ?
      //&(query->query) = NULL;
    }
  }

  return 0;
}

static int jts_query_get(void *p, Janet key, Janet *out) {
  (void) p;

  if (!janet_checktype(key, JANET_KEYWORD)) {
    return 0;
  }

  return janet_getmethod(janet_unwrap_keyword(key), query_methods, out);
}

////////


/**
 * Create a new cursor for executing a given query.
 *
 * The cursor stores the state that is needed to iteratively search
 * for matches. To use the query cursor, first call `ts_query_cursor_exec`
 * to start running a given query on a given syntax node. Then, there are
 * two options for consuming the results of the query:
 * 1. Repeatedly call `ts_query_cursor_next_match` to iterate over all of the
 *    *matches* in the order that they were found. Each match contains the
 *    index of the pattern that matched, and an array of captures. Because
 *    multiple patterns can match the same set of nodes, one match may contain
 *    captures that appear *before* some of the captures from a previous match.
 * 2. Repeatedly call `ts_query_cursor_next_capture` to iterate over all of the
 *    individual *captures* in the order that they appear. This is useful if
 *    don't care about which pattern matched, and just want a single ordered
 *    sequence of captures.
 *
 * If you don't care about consuming all of the results, you can stop calling
 * `ts_query_cursor_next_match` or `ts_query_cursor_next_capture` at any point.
 *  You can then start executing another query on another node by calling
 *  `ts_query_cursor_exec` again.
 */
static Janet cfun_query_cursor_new(int32_t argc, Janet *argv) {
  janet_fixarity(argc, 0);

  TSQueryCursor *tsquerycursor = ts_query_cursor_new();

  // XXX: can failure occur?
  if (!tsquerycursor) {
    return janet_wrap_nil();
  }

  QueryCursor *qc =
    (QueryCursor *)janet_abstract(&jts_query_cursor_type,
                                  sizeof(QueryCursor));

  qc->query_cursor = tsquerycursor;

  return janet_wrap_abstract(qc);
}

//TSQueryCursor *ts_query_cursor_new(void);

/**
 * Start running a given query on a given node.
 */
static Janet cfun_query_cursor_exec(int32_t argc, Janet *argv) {
  janet_fixarity(argc, 3);

  QueryCursor *query_cursor =
    janet_getabstract(argv, 0, &jts_query_cursor_type);
  // XXX: error-checking?

  Query *query = janet_getabstract(argv, 1, &jts_query_type);
  // XXX; error-checking?

  TSNode node = *jts_get_node(argv, 2);
  if (ts_node_is_null(node)) {
    return janet_wrap_nil();
  }

  ts_query_cursor_exec(query_cursor->query_cursor,
                       query->query,
                       node);

  // XXX: how to tell apart failure?
  return janet_wrap_nil();
}
//void ts_query_cursor_exec(TSQueryCursor *, const TSQuery *, TSNode);

/**
 * Advance to the next match of the currently running query.
 *
 * If there is a match, write it to `*match` and return `true`.
 * Otherwise, return `false`.
 */
static Janet cfun_query_cursor_next_match(int32_t argc, Janet *argv) {
  janet_fixarity(argc, 1);

  QueryCursor *query_cursor =
    janet_getabstract(argv, 0, &jts_query_cursor_type);
  // XXX: error-checking?

  TSQueryCursor *tsquerycursor = query_cursor->query_cursor;

  TSQueryMatch match;

  bool result = ts_query_cursor_next_match(tsquerycursor, &match);

  if (!result) {
    return janet_wrap_nil();
  }

  // sample return value - 3-tuple: id, pattern_index, captures
  //
  // (0                           <- id
  //  0                           <- pattern_index
  //  ((0 <tree-sitter/node ...>)
  //   (1 <tree-sitter/node ...>) <- captures
  //   ...))
  Janet *tup = janet_tuple_begin(3);

  tup[0] = janet_wrap_integer(match.id);
  tup[1] = janet_wrap_integer(match.pattern_index);

  Janet *ctup = janet_tuple_begin(match.capture_count);

  for (uint32_t i = 0; i < match.capture_count; i++) {
    // XXX: no consistency checking?
    TSNode node = match.captures[i].node;
    uint32_t index = match.captures[i].index;

    Janet *itup = janet_tuple_begin(2);

    itup[0] = janet_wrap_integer(index);

    TSNode *node_p =
      (TSNode *)janet_abstract(&jts_node_type, sizeof(TSNode));

    *node_p = node;
    itup[1] = janet_wrap_abstract(node_p);

    ctup[i] = janet_wrap_tuple(janet_tuple_end(itup));
  }

  tup[2] = janet_wrap_tuple(janet_tuple_end(ctup));

  return janet_wrap_tuple(janet_tuple_end(tup));
}
//bool ts_query_cursor_next_match(TSQueryCursor *, TSQueryMatch *match);

static const JanetMethod query_cursor_methods[] = {
  {"exec", cfun_query_cursor_exec},
  /*
    {"did-exceed-match-limit", cfun_query_cursor_did_exceed_match_limit},
    {"match-limit", cfun_query_cursor_match_limit},
    {"set-match-limit", cfun_query_cursor_set_match_limit},
    {"set-byte-range", cfun_query_cursor_set_byte_range},
    {"set-point-range", cfun_query_cursor_set_point_range},
  */
  {"next-match", cfun_query_cursor_next_match},
  /*
    {"remove-match", cfun_query_cursor_remove_match},
    {"next-capture", cfun_query_cursor_next_capture},
  */
  {NULL, NULL}
};

static int jts_query_cursor_gc(void *p, size_t size) {
  (void) size;

  QueryCursor *query_cursor = (QueryCursor *)p;
  if (query_cursor) {
    if (NULL != query_cursor->query_cursor) {
      ts_query_cursor_delete(query_cursor->query_cursor);
      // XXX: ?
      //&(query_cursor->query_cursor) = NULL;
    }
  }

  return 0;
}

static int jts_query_cursor_get(void *p, Janet key, Janet *out) {
  (void) p;

  if (!janet_checktype(key, JANET_KEYWORD)) {
    return 0;
  }

  return janet_getmethod(janet_unwrap_keyword(key),
                         query_cursor_methods,
                         out);
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
    "Return new cursor for `node`.\n"
  },
  // XXX: params ok?  update docs when determined
  {
    "_query", cfun_query_new,
    "(_tree-sitter/_query lang-name src)\n\n"
    "Return new query for `lang-name` and `src`.\n"
  },
  {
    "_query-cursor", cfun_query_cursor_new,
    "(_tree-sitter/_query-cursor)\n\n"
    "Return new query cursor.\n"
  },
  {NULL, NULL, NULL}
};

JANET_MODULE_ENTRY(JanetTable *env) {
  janet_register_abstract_type(&jts_query_cursor_type);
  janet_register_abstract_type(&jts_query_type);
  janet_register_abstract_type(&jts_cursor_type);
  janet_register_abstract_type(&jts_parser_type);
  janet_register_abstract_type(&jts_tree_type);
  janet_register_abstract_type(&jts_node_type);
  janet_cfuns(env, "tree-sitter", cfuns);
}

