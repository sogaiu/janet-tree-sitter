#include <janet.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include "tree_sitter/api.h"

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

static int jts_language_get(void *p, Janet key, Janet *out);

const JanetAbstractType jts_language_type = {
  "tree-sitter/language",
  NULL,
  NULL,
  jts_language_get,
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

static int jts_tree_gc(void *p, size_t size);

static int jts_tree_get(void *p, Janet key, Janet *out);

const JanetAbstractType jts_tree_type = {
  "tree-sitter/tree",
  jts_tree_gc,
  NULL,
  jts_tree_get,
  JANET_ATEND_GET
};

static int jts_node_get(void *p, Janet key, Janet *out);

const JanetAbstractType jts_node_type = {
  "tree-sitter/node",
  NULL,
  NULL,
  jts_node_get,
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
  if (NULL == path) {
    fprintf(stderr, "path to shared object unspecified\n");
    return janet_wrap_nil();
  }

  Clib lib = load_clib(path);
  if (NULL == lib) {
    fprintf(stderr, "%s\n", error_clib());
    return janet_wrap_nil();
  }

  const char *fn_name = (const char *)janet_getstring(argv, 1);
  if (NULL == fn_name) {
    fprintf(stderr, "function name unspecified\n");
    return janet_wrap_nil();
  }

  JTSLang jtsl = (JTSLang)symbol_clib(lib, fn_name);
  if (NULL == jtsl) {
    fprintf(stderr, "could not find the target grammar's initializer\n");
    return janet_wrap_nil();
  }

  TSParser **parser_pp =
    (TSParser **)janet_abstract(&jts_parser_type, sizeof(TSParser *));
  *parser_pp = ts_parser_new();

  if (NULL == *parser_pp) {
    fprintf(stderr, "ts_parser_new failed\n");
    return janet_wrap_nil();
  }

  if (!ts_parser_set_language(*parser_pp, jtsl())) {
    ts_parser_delete(*parser_pp);
    fprintf(stderr, "ts_parser_set_language failed\n");
    return janet_wrap_nil();
  }

  return janet_wrap_abstract(parser_pp);
}

//////// end cfun_ts_init ////////

static TSLanguage **jts_get_language(const Janet *argv, int32_t n) {
  return (TSLanguage **)janet_getabstract(argv, n, &jts_language_type);
}

/**
 * Get the ABI version number for this language. This version number is used
 * to ensure that languages were generated by a compatible version of
 * Tree-sitter.
 *
 * See also `ts_parser_set_language`.
 */
static Janet cfun_language_version(int32_t argc, Janet *argv) {
  janet_fixarity(argc, 1);

  TSLanguage **lang_pp = jts_get_language(argv, 0);
  // XXX: error checking?

  return janet_wrap_integer(ts_language_version(*lang_pp));
}

static const JanetMethod language_methods[] = {
  //{"symbol-count", cfun_language_symbol_count},
  //{"symbol-name", cfun_language_symbol_name},
  //{"symbol-for-name", cfun_language_symbol_for_name},
  //{"field-count", cfun_language_field_count},
  //{"field-name-for-id", cfun_language_field_name_for_id},
  //{"field-name-for-name", cfun_language_field_name_for_name},
  //{"symbol-type", cfun_language_symbol_type},
  {"version", cfun_language_version},
  {NULL, NULL}
};

static int jts_language_get(void *p, Janet key, Janet *out) {
  (void) p;

  if (!janet_checktype(key, JANET_KEYWORD)) {
    return 0;
  }

  return janet_getmethod(janet_unwrap_keyword(key), language_methods, out);
}

////////

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
  if (NULL == the_type) {
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
 * Get an S-expression representing the node as a string.
 *
 * This string is allocated with `malloc` and the caller is responsible for
 * freeing it using `free`.
 */
static Janet cfun_node_string(int32_t argc, Janet *argv) {
  janet_fixarity(argc, 1);

  TSNode node = *jts_get_node(argv, 0);
  if (ts_node_is_null(node)) {
    return janet_wrap_nil();
  }

  char *text = ts_node_string(node);
  if (NULL == text) {
    return janet_wrap_nil();
  }

  return janet_cstringv(text);
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
 * Get the field name for node's child at the given index, where zero represents
 * the first child. Returns NULL, if no field is found.
 */
//const char *ts_node_field_name_for_child(TSNode, uint32_t);

/**
 * Get the node's number of children.
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

  *child_p = ts_node_named_child(node, idx);
  if (ts_node_is_null(*child_p)) {
    return janet_wrap_nil();
  }

  return janet_wrap_abstract(child_p);
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
 * Get the node's child with the given field name.
 */
/*
TSNode ts_node_child_by_field_name(
  TSNode self,
  const char *field_name,
  uint32_t field_name_length
);
*/

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

static Janet cfun_node_tree(int32_t argc, Janet *argv) {
  janet_fixarity(argc, 1);

  TSNode node = *jts_get_node(argv, 0);
  if (ts_node_is_null(node)) {
    return janet_wrap_nil();
  }

  TSTree **tree_pp =
    (TSTree **)janet_abstract(&jts_tree_type, sizeof(TSTree *));

  // XXX: casting to avoid warning, but don't really want to
  //      allow *tree_pp to be modified after this point?
  *tree_pp = (TSTree *)node.tree;

  return janet_wrap_abstract(*tree_pp);
}

static Janet cfun_node_text(int32_t argc, Janet *argv) {
  janet_fixarity(argc, 2);

  TSNode node = *jts_get_node(argv, 0);
  if (ts_node_is_null(node)) {
    return janet_wrap_nil();
  }

  const char *source = (const char *)janet_getstring(argv, 1);
  if (NULL == source) {
    return janet_wrap_nil();
  }

  uint32_t start = ts_node_start_byte(node);
  uint32_t end = ts_node_end_byte(node);

  // XXX: could this be a problem if difference is large?
  int32_t len = (int32_t)(end - start);

  return janet_stringv((const uint8_t *)source + start, len);
}

static const JanetMethod node_methods[] = {
  {"type", cfun_node_type},
  //{"symbol", cfun_node_symbol},
  {"start-byte", cfun_node_start_byte},
  {"start-point", cfun_node_start_point},
  {"end-byte", cfun_node_end_byte},
  {"end-point", cfun_node_end_point},
  {"string", cfun_node_string},
  {"is-null", cfun_node_is_null},
  {"is-named", cfun_node_is_named},
  //{"is-missing", cfun_node_is_missing},
  //{"is-extra", cfun_node_is_extra},
  //{"has-changes", cfun_node_has_changes},
  {"has-error", cfun_node_has_error},
  {"parent", cfun_node_parent},
  {"child", cfun_node_child},
  //{"field-name-for-child", cfun_node_field_name_for_child},
  {"child-count", cfun_node_child_count},
  {"named-child", cfun_node_named_child},
  {"named-child-count", cfun_node_named_child_count},
  //{"child-by-field-name", cfun_node_child_by_field-name},
  //{"child-by-field-id", cfun_node_child_by_field-id},
  {"next-sibling", cfun_node_next_sibling},
  {"prev-sibling", cfun_node_prev_sibling},
  //{"next-named-sibling", cfun_node_next_named_sibling},
  //{"prev-named-sibling", cfun_node_prev_named_sibling},
  {"first-child-for-byte", cfun_node_first_child_for_byte},
  {"first-named-child-for-byte", cfun_node_first_named_child_for_byte},
  {"descendant-for-byte-range", cfun_node_descendant_for_byte_range},
  {"descendant-for-point-range", cfun_node_descendant_for_point_range},
  /*
  {"named-descendant-for-byte-range",
   cfun_node_named_descendant_for_byte_range},
  {"named-descendant-for-point-range",
   cfun_node_named_descendant_for_point_range},
  */
  //{"edit", cfun_node_edit}
  {"eq", cfun_node_eq},
  // custom
  {"expr", cfun_node_string}, // alias for backward compatibility
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

static TSTree **jts_get_tree(const Janet *argv, int32_t n) {
  return (TSTree **)janet_getabstract(argv, n, &jts_tree_type);
}

/**
 * Get the root node of the syntax tree.
 */
static Janet cfun_tree_root_node(int32_t argc, Janet *argv) {
  janet_fixarity(argc, 1);

  TSTree **tree_pp = jts_get_tree(argv, 0);
  // XXX: error checking?

  TSNode *node_p =
    (TSNode *)janet_abstract(&jts_node_type, sizeof(TSNode));

  *node_p = ts_tree_root_node(*tree_pp);
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

  TSTree **tree_pp = jts_get_tree(argv, 0);
  // XXX: error checking?

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

  TSInputEdit input_edit = (TSInputEdit) {
    .start_byte = start_byte,
    .old_end_byte = old_end_byte,
    .new_end_byte = new_end_byte,
    .start_point = start_point,
    .old_end_point = old_end_point,
    .new_end_point = new_end_point
  };

  ts_tree_edit(*tree_pp, &input_edit);

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

  TSTree **old_tree_pp = jts_get_tree(argv, 0);
  TSTree **new_tree_pp = jts_get_tree(argv, 1);
  // XXX: error checking?

  uint32_t length = 0;

  TSRange *range =
    ts_tree_get_changed_ranges(*old_tree_pp, *new_tree_pp, &length);

  if (length == 0) {
    free(range);
    return janet_wrap_nil();
  }

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

  TSTree **tree_pp = jts_get_tree(argv, 0);
  // XXX: error checking?

  // XXX: is this safe?
  JanetFile *of =
    (JanetFile *)janet_getabstract(argv, 1, &janet_file_type);

  // XXX: check of->flags to make sure writable?
  //      what about append, etc.?
  if (!(of->flags & JANET_FILE_WRITE)) {
    return janet_wrap_nil();
  }

  ts_tree_print_dot_graph(*tree_pp, of->file);

  return janet_wrap_nil();
}

static const JanetMethod tree_methods[] = {
  //{"copy", cfun_tree_copy},
  //{"delete", cfun_tree_delete},
  {"root-node", cfun_tree_root_node},
  //{"root-node-with-offset", cfun_tree_root_node_with_offset},
  //{"language", cfun_tree_language},
  //{"included-ranges", cfun_tree_included_ranges},
  {"edit", cfun_tree_edit},
  {"get-changed-ranges", cfun_tree_get_changed_ranges},
  {"print-dot-graph", cfun_tree_print_dot_graph},
  {NULL, NULL}
};

static int jts_tree_gc(void *p, size_t size) {
  (void) size;

  TSTree **tree_pp = (TSTree **)p;
  if (*tree_pp != NULL) {
    ts_tree_delete(*tree_pp);
    *tree_pp = NULL;
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

static TSParser **jts_get_parser(const Janet *argv, int32_t n) {
  return (TSParser **)janet_getabstract(argv, n, &jts_parser_type);
}

/**
 * Get the parser's current language.
 */
static Janet cfun_parser_language(int32_t argc, Janet *argv) {
  janet_fixarity(argc, 1);

  TSParser **parser_pp = jts_get_parser(argv, 0);

  TSLanguage **lang_pp =
    (TSLanguage **)janet_abstract(&jts_language_type, sizeof(TSLanguage *));

  // XXX: casting to avoid warning, but don't really want to
  //      allow *lang_pp to be modified after this point?
  *lang_pp = (TSLanguage *)ts_parser_language(*parser_pp);
  if (NULL == *lang_pp) {
    return janet_wrap_nil();
  }

  return janet_wrap_abstract(lang_pp);
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
    line = (const char *)janet_string(buf->data, buf->count);
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

/**
 * Use the parser to parse some source code and create a syntax tree.
 *
 * If you are parsing this document for the first time, pass `NULL` for the
 * `old_tree` parameter. Otherwise, if you have already parsed an earlier
 * version of this document and the document has since been edited, pass the
 * previous syntax tree so that the unchanged parts of it can be reused.
 * This will save time and memory. For this to work correctly, you must have
 * already edited the old syntax tree using the `ts_tree_edit` function in a
 * way that exactly matches the source code changes.
 *
 * The `TSInput` parameter lets you specify how to read the text. It has the
 * following three fields:
 * 1. `read`: A function to retrieve a chunk of text at a given byte offset
 *    and (row, column) position. The function should return a pointer to the
 *    text and write its length to the `bytes_read` pointer. The parser does
 *    not take ownership of this buffer; it just borrows it until it has
 *    finished reading it. The function should write a zero value to the
 *    `bytes_read` pointer to indicate the end of the document.
 * 2. `payload`: An arbitrary pointer that will be passed to each invocation
 *    of the `read` function.
 * 3. `encoding`: An indication of how the text is encoded. Either
 *    `TSInputEncodingUTF8` or `TSInputEncodingUTF16`.
 *
 * This function returns a syntax tree on success, and `NULL` on failure. There
 * are three possible reasons for failure:
 * 1. The parser does not have a language assigned. Check for this using the
      `ts_parser_language` function.
 * 2. Parsing was cancelled due to a timeout that was set by an earlier call to
 *    the `ts_parser_set_timeout_micros` function. You can resume parsing from
 *    where the parser left out by calling `ts_parser_parse` again with the
 *    same arguments. Or you can start parsing from scratch by first calling
 *    `ts_parser_reset`.
 * 3. Parsing was cancelled using a cancellation flag that was set by an
 *    earlier call to `ts_parser_set_cancellation_flag`. You can resume parsing
 *    from where the parser left out by calling `ts_parser_parse` again with
 *    the same arguments.
 */
static Janet cfun_parser_parse(int32_t argc, Janet *argv) {
  janet_fixarity(argc, 3);

  TSParser **parser_pp = jts_get_parser(argv, 0);

  TSTree *old_tree_p;

  Janet x = argv[1];
  if (janet_checktype(x, JANET_NIL)) {
    old_tree_p = NULL;
  } else if (janet_checktype(x, JANET_ABSTRACT)) {
    TSTree **temp_tree_pp = jts_get_tree(argv, 1);
    if (NULL == temp_tree_pp) {
      return janet_wrap_nil();
    }

    old_tree_p = *temp_tree_pp;
  } else {
    return janet_wrap_nil();
  }

  JanetArray *lines = janet_getarray(argv, 2);

  TSInput input = (TSInput) {
    .payload = (void *)lines,
    .read = &jts_read_lines_fn,
    .encoding = TSInputEncodingUTF8
  };

  TSTree *new_tree_p = ts_parser_parse(*parser_pp, old_tree_p, input);
  if (NULL == new_tree_p) {
    return janet_wrap_nil();
  }

  TSTree **tree_pp =
    (TSTree **)janet_abstract(&jts_tree_type, sizeof(TSTree *));

  *tree_pp = new_tree_p;

  return janet_wrap_abstract(tree_pp);
}

/**
 * Use the parser to parse some source code stored in one contiguous buffer.
 * The first two parameters are the same as in the `ts_parser_parse` function
 * above. The second two parameters indicate the location of the buffer and its
 * length in bytes.
 */
static Janet cfun_parser_parse_string(int32_t argc, Janet *argv) {
  janet_arity(argc, 2, 3);

  TSParser **parser_pp = jts_get_parser(argv, 0);

  TSTree *old_tree_p = NULL;

  uint32_t s_idx;

  if (argc == 2) {
    s_idx = 1;
  } else {
    TSTree **temp_tree_pp = jts_get_tree(argv, 1);
    if (NULL == temp_tree_pp) {
      return janet_wrap_nil();
    }

    old_tree_p = *temp_tree_pp;

    s_idx = 2;
  }

  const char *src = (const char *)janet_getstring(argv, s_idx);
  if (NULL == src) {
    return janet_wrap_nil();
  }

  TSTree *new_tree_p =
    ts_parser_parse_string(*parser_pp, (const TSTree *)old_tree_p,
                           src, (uint32_t)strlen(src));
  if (NULL == new_tree_p) {
    return janet_wrap_nil();
  }

  TSTree **tree_pp =
    (TSTree **)janet_abstract(&jts_tree_type, sizeof(TSTree *));

  *tree_pp = new_tree_p;

  return janet_wrap_abstract(tree_pp);
}

void log_by_eprint(void *payload, TSLogType type, const char *message) {
  if (type == TSLogTypeLex) {
    janet_eprintf("  %s\n", message);
  } else {
    janet_eprintf("%s\n", message);
  }
}

static Janet cfun_parser_log_by_eprint(int32_t argc, Janet *argv) {
  janet_fixarity(argc, 1);

  TSParser **parser_pp = jts_get_parser(argv, 0);

  TSLogger logger = {*parser_pp, log_by_eprint};

  ts_parser_set_logger(*parser_pp, logger);

  return janet_wrap_nil();
}

static Janet cfun_parser_print_dot_graphs_0(int32_t argc, Janet *argv) {
  janet_fixarity(argc, 2);

  TSParser **parser_pp = jts_get_parser(argv, 0);

  // XXX: is this safe?
  JanetFile *of =
    (JanetFile *)janet_getabstract(argv, 1, &janet_file_type);
  if (NULL == of) {
    return janet_wrap_nil();
  }

  // XXX: check of->flags to make sure writable?
  //      what about append, etc.?
  if (!(of->flags & JANET_FILE_WRITE)) {
    return janet_wrap_nil();
  }

  // XXX: britle?
  // XXX: ended up including portions of parser.c near beginning
  //      of this file to get this working...
  (*parser_pp)->dot_graph_file = of->file;

  // XXX: more useful than nil as a return value?
  return janet_wrap_true();
}

static const JanetMethod parser_methods[] = {
  //{"new", cfun_parser_new},
  //{"delete", cfun_parser_delete},
  //{"set-language", cfun_parser_set_language},
  {"language", cfun_parser_language},
  //{"set-included-ranges", cfun_parser_set_included_ranges},
  //{"included-ranges", cfun_parser_included_ranges},
  {"parse", cfun_parser_parse},
  {"parse-string", cfun_parser_parse_string},
  //{"parse-string-encoding", cfun_parser_parse_string_encoding},
  //{"reset", cfun_parser_reset},
  //{"set-timeout-micros", cfun_parser_set_timeout_micros},
  //{"timeout-micros", cfun_parser_timeout_micros},
  //{"set-cancellation-flag", cfun_parser_set_cancellation_flag},
  //{"cancellation-flag", cfun_parser_cancellation_flag},
  //{"set-logger", cfun_parser_set_logger},
  //{"logger", cfun_parser_logger},
  //{"print-dot-graphs", cfun_parser_print_dot_graphs},
  // custom
  {"print-dot-graphs-0", cfun_parser_print_dot_graphs_0},
  {"log-by-eprint", cfun_parser_log_by_eprint},
  {NULL, NULL}
};

static int jts_parser_gc(void *p, size_t size) {
  (void) size;

  TSParser **parser_pp = (TSParser **)p;
  if (*parser_pp != NULL) {
    ts_parser_delete(*parser_pp);
    *parser_pp = NULL;
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

static TSTreeCursor *jts_get_cursor(const Janet *argv, int32_t n) {
  return (TSTreeCursor *)janet_getabstract(argv, n, &jts_cursor_type);
}

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

  TSTreeCursor *cursor_p =
    (TSTreeCursor *)janet_abstract(&jts_cursor_type, sizeof(TSTreeCursor));
  *cursor_p = c;

  return janet_wrap_abstract(cursor_p);
}

/**
 * Re-initialize a tree cursor to start at a different node.
 */
static Janet cfun_cursor_reset(int32_t argc, Janet *argv) {
  janet_fixarity(argc, 2);

  TSTreeCursor *cursor_p = jts_get_cursor(argv, 0);
  // XXX: error checking?

  TSNode node = *jts_get_node(argv, 1);
  if (ts_node_is_null(node)) {
    return janet_wrap_nil();
  }

  ts_tree_cursor_reset(cursor_p, node);

  // XXX: better to return true?
  return janet_wrap_nil();
}

/**
 * Get the tree cursor's current node.
 */
static Janet cfun_cursor_current_node(int32_t argc, Janet *argv) {
  janet_fixarity(argc, 1);

  TSTreeCursor *cursor_p = jts_get_cursor(argv, 0);
  // XXX: error checking?

  TSNode *node_p =
    (TSNode *)janet_abstract(&jts_node_type, sizeof(TSNode));

  *node_p = ts_tree_cursor_current_node(cursor_p);
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

  TSTreeCursor *cursor_p = jts_get_cursor(argv, 0);
  // XXX: error checking?

  const char *name = ts_tree_cursor_current_field_name(cursor_p);
  if (NULL == name) {
    return janet_wrap_nil();
  }

  return janet_cstringv(name);
}

/**
 * Move the cursor to the parent of its current node.
 *
 * This returns `true` if the cursor successfully moved, and returns `false`
 * if there was no parent node (the cursor was already on the root node).
 */
static Janet cfun_cursor_goto_parent(int32_t argc, Janet *argv) {
  janet_fixarity(argc, 1);

  TSTreeCursor *cursor_p = jts_get_cursor(argv, 0);
  // XXX: error checking?

  if (ts_tree_cursor_goto_parent(cursor_p)) {
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

  TSTreeCursor *cursor_p = jts_get_cursor(argv, 0);
  // XXX: error checking?

  if (ts_tree_cursor_goto_next_sibling(cursor_p)) {
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

  TSTreeCursor *cursor_p = jts_get_cursor(argv, 0);
  // XXX: error checking?

  if (ts_tree_cursor_goto_first_child(cursor_p)) {
    return janet_wrap_true();
  } else {
    return janet_wrap_false();
  }
}

static const JanetMethod cursor_methods[] = {
  //{"delete", cfun_cursor_delete},
  {"reset", cfun_cursor_reset},
  {"current-node", cfun_cursor_current_node},
  {"current-field-name", cfun_cursor_current_field_name},
  //{"current-field-id", cfun_cursor_current_field_id},
  {"goto-parent", cfun_cursor_goto_parent},
  {"goto-next-sibling", cfun_cursor_goto_next_sibling},
  {"goto-first-child", cfun_cursor_goto_first_child},
  //{"goto-first-child-for-byte", cfun_cursor_goto_first_child_for_byte},
  //{"goto-first-child-for-point", cfun_cursor_goto_first_child_for_point},
  //{"copy", cfun_cursor_copy},
  // custom - convenience aliases
  {"node", cfun_cursor_current_node},
  {"field-name", cfun_cursor_current_field_name},
  {"go-parent", cfun_cursor_goto_parent},
  {"go-next-sibling", cfun_cursor_goto_next_sibling},
  {"go-first-child", cfun_cursor_goto_first_child},
  {NULL, NULL}
};

static int jts_cursor_gc(void *p, size_t size) {
  (void) size;

  TSTreeCursor *cursor_p = (TSTreeCursor *)p;
  if (cursor_p != NULL) {
    ts_tree_cursor_delete(cursor_p);
    cursor_p = NULL;
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

static TSQuery **jts_get_query(const Janet *argv, uint32_t n) {
  return (TSQuery **)janet_getabstract(argv, n, &jts_query_type);
}

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

  TSLanguage **lang_pp = jts_get_language(argv, 0);

  const char *src = (const char *)janet_getstring(argv, 1);
  if (NULL == src) {
    return janet_wrap_nil();
  }

  // XXX: is this off by one?
  uint32_t src_len = (uint32_t)strlen(src);

  uint32_t error_offset;
  TSQueryError error_type;

  TSQuery *query_p =
    ts_query_new(*lang_pp, src, src_len, &error_offset, &error_type);
  if (NULL == query_p) {
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

  TSQuery **q_pp =
    (TSQuery **)janet_abstract(&jts_query_type, sizeof(TSQuery *));

  *q_pp = query_p;

  return janet_wrap_abstract(q_pp);
}

/**
 * Get the name and length of one of the query's captures, or one of the
 * query's string literals. Each capture and string is associated with a
 * numeric id based on the order that it appeared in the query's source.
 */
static Janet cfun_query_capture_name_for_id(int32_t argc, Janet *argv) {
  janet_fixarity(argc, 2);

  TSQuery **query_pp = jts_get_query(argv, 0);
  // XXX; error checking?

  uint32_t id = (uint32_t)janet_getinteger(argv, 1);

  uint32_t length;

  const char *name = ts_query_capture_name_for_id(*query_pp, id, &length);
  if (NULL == name) {
    return janet_wrap_nil();
  }

  Janet *tup = janet_tuple_begin(2);

  tup[0] = janet_cstringv(name);
  tup[1] = janet_wrap_integer(length);

  return janet_wrap_tuple(janet_tuple_end(tup));
}

static const JanetMethod query_methods[] = {
  //{"delete", cfun_query_delete},
  //{"pattern-count", cfun_query_pattern_count},
  //{"capture-count", cfun_query_capture_count},
  //{"string-count", cfun_query_string_count},
  //{"start-byte-for-pattern", cfun_query_start_byte_for_pattern},
  //{"predicates-for-pattern", cfun_query_predicates_for_pattern},
  //{"is-pattern-rooted", cfun_query_is_pattern_rooted},
  //{"is-pattern-guaranteed-at-step", cfun_query_is_pattern_guaranteed_at_step},
  {"capture-name-for-id", cfun_query_capture_name_for_id},
  //{"capture-quantifier-for-id", cfun_query_capture_quantifier_for_id},
  //{"string-value-for-id", cfun_query_string_value_for_id},
  //{"disable-capture", cfun_query_disable_capture},
  //{"disable-pattern", cfun_query_disable_pattern},
  {NULL, NULL}
};

static int jts_query_gc(void *p, size_t size) {
  (void) size;

  TSQuery **query_pp = (TSQuery **)p;
  if (*query_pp != NULL) {
    ts_query_delete(*query_pp);
    *query_pp = NULL;
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

static TSQueryCursor **jts_get_query_cursor(const Janet *argv, uint32_t n) {
  return (TSQueryCursor **)janet_getabstract(argv, n, &jts_query_cursor_type);
}

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

  TSQueryCursor **qc_pp =
    (TSQueryCursor **)janet_abstract(&jts_query_cursor_type,
                                     sizeof(TSQueryCursor *));
  *qc_pp = ts_query_cursor_new();
  if (NULL == *qc_pp) {
    return janet_wrap_nil();
  }

  return janet_wrap_abstract(qc_pp);
}

/**
 * Start running a given query on a given node.
 */
static Janet cfun_query_cursor_exec(int32_t argc, Janet *argv) {
  janet_fixarity(argc, 3);

  TSQueryCursor **qc_pp = jts_get_query_cursor(argv, 0);
  // XXX: error checking?

  TSQuery **query_pp = jts_get_query(argv, 1);
  // XXX; error checking?

  TSNode node = *jts_get_node(argv, 2);
  if (ts_node_is_null(node)) {
    return janet_wrap_nil();
  }

  // XXX: no failure indication
  ts_query_cursor_exec(*qc_pp, *query_pp, node);

  return janet_wrap_nil();
}

/**
 * Advance to the next match of the currently running query.
 *
 * If there is a match, write it to `*match` and return `true`.
 * Otherwise, return `false`.
 */
static Janet cfun_query_cursor_next_match(int32_t argc, Janet *argv) {
  janet_fixarity(argc, 1);

  TSQueryCursor **qc_pp = jts_get_query_cursor(argv, 0);
  // XXX: error checking?

  TSQueryMatch match;

  if (!ts_query_cursor_next_match(*qc_pp, &match)) {
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

static const JanetMethod query_cursor_methods[] = {
  //{"delete", cfun_query_cursor_delete},
  {"exec", cfun_query_cursor_exec},
  //{"did-exceed-match-limit", cfun_query_cursor_did_exceed_match_limit},
  //{"match-limit", cfun_query_cursor_match_limit},
  //{"set-match-limit", cfun_query_cursor_set_match_limit},
  //{"set-byte-range", cfun_query_cursor_set_byte_range},
  //{"set-point-range", cfun_query_cursor_set_point_range},
  {"next-match", cfun_query_cursor_next_match},
  //{"remove-match", cfun_query_cursor_remove_match},
  //{"next-capture", cfun_query_cursor_next_capture},
  {NULL, NULL}
};

static int jts_query_cursor_gc(void *p, size_t size) {
  (void) size;

  TSQueryCursor **qc_pp = (TSQueryCursor **)p;
  if (*qc_pp != NULL) {
    ts_query_cursor_delete(*qc_pp);
    *qc_pp = NULL;
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
  janet_register_abstract_type(&jts_language_type);
  janet_register_abstract_type(&jts_parser_type);
  janet_register_abstract_type(&jts_tree_type);
  janet_register_abstract_type(&jts_node_type);
  janet_register_abstract_type(&jts_cursor_type);
  janet_register_abstract_type(&jts_query_type);
  janet_register_abstract_type(&jts_query_cursor_type);
  janet_cfuns(env, "tree-sitter", cfuns);
}

