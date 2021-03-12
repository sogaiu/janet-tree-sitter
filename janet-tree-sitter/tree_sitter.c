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

static char *get_processed_name(const char *name) {
    if (name[0] == '.') return (char *) name;
    const char *c;
    for (c = name; *c; c++) {
        if (*c == '/') return (char *) name;
    }
    size_t l = (size_t)(c - name);
    char *ret = malloc(l + 3);
    if (NULL == ret) {
      fprintf(stderr, "out of memory\n");
      exit(1);
    }
    ret[0] = '.';
    ret[1] = '/';
    memcpy(ret + 2, name, l + 1);
    return ret;
}

////////

typedef TSLanguage* (*JTSLang)(void);

////////

typedef struct {
  TSNode node;
} Node;

typedef struct {
  TSTree* tree;
} Tree;

typedef struct {
  TSParser* parser;
} Parser;

static int jts_node_get(void* p, Janet key, Janet* out);

const JanetAbstractType jts_node_type = {
  "tree-sitter/node",
  NULL,
  NULL,
  jts_node_get,
  JANET_ATEND_GET
};

static int jts_tree_gc(void *p, size_t size);

static int jts_tree_get(void* p, Janet key, Janet* out);

const JanetAbstractType jts_tree_type = {
  "tree-sitter/tree",
  jts_tree_gc,
  NULL,
  jts_tree_get,
  JANET_ATEND_GET
};

static int jts_parser_gc(void *p, size_t size);

static int jts_parser_get(void* p, Janet key, Janet* out);

const JanetAbstractType jts_parser_type = {
  "tree-sitter/parser",
  jts_parser_gc,
  NULL,
  jts_parser_get,
  JANET_ATEND_GET
};

////////

// XXX: it's likely this could use a lot of improvement :)
char* ts_fn_name_from_path(const char* path) {
  // XXX: enough?
  char g_name[128] = "";
  char* temp = path;

  // XXX: what about windows?
  const char* slash = strrchr(path, '/');
  if (slash != NULL) {
    // XXX: how to check if this is ok to do?
    temp = slash + 1;
  }

  const char* dot = strrchr(temp, '.');
  if (dot != NULL) {
    // XXX: is it safe to use slash here when it is NULL?
    strncpy(g_name, temp, dot - slash - 1);
  }

  char* ts_prefix = "tree_sitter_";

  char* fn_name = malloc(strlen(ts_prefix) + strlen(g_name));
  if (NULL == fn_name) {
    fprintf(stderr, "out of memory\n");
    exit(1);
  }

  strncpy(fn_name, ts_prefix, strlen(ts_prefix));
  strncpy(fn_name + strlen(ts_prefix), g_name, strlen(g_name));

  return fn_name;
}

char* ts_fn_name_from_grammar_name(const char* name) {
  char* ts_prefix = "tree_sitter_";

  char* fn_name = malloc(strlen(ts_prefix) + strlen(name) + 1);
  if (NULL == fn_name) {
    fprintf(stderr, "out of memory\n");
    exit(1);
  }

  char* from = fn_name;
  strncpy(from, ts_prefix, strlen(ts_prefix));
  from += strlen(ts_prefix);
  strncpy(fn_name + strlen(ts_prefix), name, strlen(name));
  from += strlen(name);
  // null-terminate
  *from = '\0';

  return fn_name;
}

char* ts_lib_path_from_grammar_name(const char* name) {
  // XXX: does it matter that getenv is not thread-safe?
  char* home_dir = getenv("HOME");
  if (strlen(home_dir) == 0) {
    home_dir = getenv("USER_PROFILE");
    if (strlen(home_dir) == 0) {
      fprintf(stderr, "HOME or USER_PROFILE must be set\n");
      exit(1);
    }
  }

  const char* ts_bin_path = "/.tree-sitter/bin/";

#if defined(WIN32) || defined(_WIN32)
  char* ext = ".dll";
#else
  char* ext = ".so";
#endif

  // /home/user + /.tree-sitter/bin/ + clojure + .so (or .dll) + 0
  size_t path_len = strlen(home_dir) + strlen(ts_bin_path) + \
                    strlen(name) + strlen(ext) + 1;

  char* path = malloc(path_len);
  
  if (NULL == path) {
    fprintf(stderr, "out of memory\n");
    exit(1);
  }

  char* from = path;
  strncpy(from, home_dir, strlen(home_dir));
  from += strlen(home_dir);
  strncpy(from, ts_bin_path, strlen(ts_bin_path));
  from += strlen(ts_bin_path);
  strncpy(from, name, strlen(name));
  from += strlen(name);
  strncpy(from, ext, strlen(ext));
  from += strlen(ext);
  // null-terminate
  *from = '\0';

  return path;
}

static Janet cfun_ts_init(int32_t argc, Janet* argv) {
  janet_fixarity(argc, 1);

  const char* name = (const char *)janet_getstring(argv, 0);

  const char* fn_name = ts_fn_name_from_grammar_name(name);

  const char* path = ts_lib_path_from_grammar_name(name);

  char *processed_name = get_processed_name(path);
  Clib lib = load_clib(processed_name);
  JTSLang jtsl;

  if (name != processed_name) {
    free(processed_name);
  }

  if (!lib) {
    fprintf(stderr, error_clib());
    return janet_wrap_nil();
  }

  jtsl = (JTSLang) symbol_clib(lib, fn_name);
  if (!jtsl) {
    fprintf(stderr, "could not find the target TSLanguage symbol");
    return janet_wrap_nil();
  }

  TSParser *p = ts_parser_new();
  if (p == NULL) {
    return janet_wrap_nil();
  }

  Parser* parser =
    (Parser *)janet_abstract(&jts_parser_type, sizeof(Parser));
  parser->parser = p;

  bool success = ts_parser_set_language(p, jtsl());
  if (!success) {
    // XXX: abstract will take care of this?
    //free(p);
    return janet_wrap_nil();
  }

  return janet_wrap_abstract(parser);
}

static Janet cfun_node_eq(int32_t argc, Janet* argv) {
  janet_fixarity(argc, 2);
  // XXX: error checking?
  Node* node_l = janet_getabstract(argv, 0, &jts_node_type);
  // XXX: error checking?
  Node* node_r = janet_getabstract(argv, 1, &jts_node_type);
  if (ts_node_eq(node_l->node, node_r->node)) {
    return janet_wrap_true();
  } else {
    return janet_wrap_false();
  }
}

static Janet cfun_node_has_error(int32_t argc, Janet* argv) {
  janet_fixarity(argc, 1);
  // XXX: error checking?
  Node* node = janet_getabstract(argv, 0, &jts_node_type);
  if (ts_node_has_error(node->node)) {
    return janet_wrap_true();
  } else {
    return janet_wrap_false();
  }
}

static Janet cfun_node_is_named(int32_t argc, Janet* argv) {
  janet_fixarity(argc, 1);
  // XXX: error checking?
  Node* node = janet_getabstract(argv, 0, &jts_node_type);
  if (ts_node_is_named(node->node)) {
    return janet_wrap_true();
  } else {
    return janet_wrap_false();
  }
}

static Janet cfun_node_type(int32_t argc, Janet* argv) {
  janet_fixarity(argc, 1);
  Node* node = (Node *)janet_getabstract(argv, 0, &jts_node_type);
  // XXX: error checking?
  const char* the_type = ts_node_type(node->node);
  return janet_cstringv(the_type);
}

static Janet cfun_node_expr(int32_t argc, Janet* argv) {
  janet_fixarity(argc, 1);
  // XXX: error checking?
  Node* node = janet_getabstract(argv, 0, &jts_node_type);
  char* text = ts_node_string(node->node);
  // janet_cstring seems to help with not ending up with extra stuff at end
  return janet_wrap_string(janet_cstring(text));
}

static Janet cfun_node_start_byte(int32_t argc, Janet* argv) {
  janet_fixarity(argc, 1);
  // XXX: error checking?
  Node* node = janet_getabstract(argv, 0, &jts_node_type);
  return janet_wrap_integer(ts_node_start_byte(node->node));
}

static Janet cfun_node_end_byte(int32_t argc, Janet* argv) {
  janet_fixarity(argc, 1);
  // XXX: error checking?
  Node* node = janet_getabstract(argv, 0, &jts_node_type);
  return janet_wrap_integer(ts_node_end_byte(node->node));
}

static Janet cfun_node_child(int32_t argc, Janet* argv) {
  janet_fixarity(argc, 2);
  // XXX: error checking?
  Node* node = janet_getabstract(argv, 0, &jts_node_type);
  // XXX: how to handle negative appropriately?
  uint32_t idx = (uint32_t)janet_getinteger(argv, 1);
  Node* child =
    (Node *)janet_abstract(&jts_node_type, sizeof(Node));
  // XXX: error checking?
  child->node = ts_node_child(node->node, idx);
  if (ts_node_is_null(child->node)) {
    return janet_wrap_nil();
  }
  return janet_wrap_abstract(child);
}

static Janet cfun_node_parent(int32_t argc, Janet* argv) {
  janet_fixarity(argc, 1);
  // XXX: error checking?
  Node* node = janet_getabstract(argv, 0, &jts_node_type);
  Node* parent =
    (Node *)janet_abstract(&jts_node_type, sizeof(Node));
  // XXX: error checking?
  parent->node = ts_node_parent(node->node);
  if (ts_node_is_null(parent->node)) {
    return janet_wrap_nil();
  }
  return janet_wrap_abstract(parent);
}

static Janet cfun_node_next_sibling(int32_t argc, Janet* argv) {
  janet_fixarity(argc, 1);
  // XXX: error checking?
  Node* node = janet_getabstract(argv, 0, &jts_node_type);
  Node* sibling =
    (Node *)janet_abstract(&jts_node_type, sizeof(Node));
  // XXX: error checking?
  sibling->node = ts_node_next_sibling(node->node);
  if (ts_node_is_null(sibling->node)) {
    return janet_wrap_nil();
  }
  return janet_wrap_abstract(sibling);
}

static Janet cfun_node_prev_sibling(int32_t argc, Janet* argv) {
  janet_fixarity(argc, 1);
  // XXX: error checking?
  Node* node = janet_getabstract(argv, 0, &jts_node_type);
  Node* sibling =
    (Node *)janet_abstract(&jts_node_type, sizeof(Node));
  // XXX: error checking?
  sibling->node = ts_node_prev_sibling(node->node);
  if (ts_node_is_null(sibling->node)) {
    return janet_wrap_nil();
  }
  return janet_wrap_abstract(sibling);
}

static Janet cfun_node_child_count(int32_t argc, Janet* argv) {
  janet_fixarity(argc, 1);
  // XXX: error checking?
  Node* node = janet_getabstract(argv, 0, &jts_node_type);
  // XXX: how to handle negative appropriately?
  return janet_wrap_integer(ts_node_child_count(node->node));
}

static Janet cfun_node_tree(int32_t argc, Janet* argv) {
  janet_fixarity(argc, 1);

  Node* node = janet_getabstract(argv, 0, &jts_node_type);

  Tree* tree =
    (Tree *)janet_abstract(&jts_tree_type, sizeof(Tree));

  tree->tree = (node->node).tree;

  return janet_wrap_abstract(tree);
}

static Janet cfun_node_text(int32_t argc, Janet* argv) {
  janet_fixarity(argc, 2);

  // XXX: error checking?
  Node* node = janet_getabstract(argv, 0, &jts_node_type);

  const char* source = (const char *)janet_getstring(argv, 1);
  
  uint32_t start = ts_node_start_byte(node->node);
  uint32_t end = ts_node_end_byte(node->node);

  size_t len = end - start;
  char* text = (char*)malloc(len + 1);
  if (NULL == text) {
    fprintf(stderr, "out of memory\n");
    exit(1);
  }

  strncpy(text, source + start, len);
  text[len] = '\0';

  return janet_wrap_string(janet_cstring(text));
}

static const JanetMethod node_methods[] = {
  // XXX: fix docs
  {"eq", cfun_node_eq},
  {"has-error", cfun_node_has_error},
  {"is-named", cfun_node_is_named},
  {"type", cfun_node_type},
  {"expr", cfun_node_expr},
  {"start-byte", cfun_node_start_byte},
  {"end-byte", cfun_node_end_byte},
  {"child", cfun_node_child},
  {"parent", cfun_node_parent},
  {"next-sibling", cfun_node_next_sibling},
  {"prev-sibling", cfun_node_prev_sibling},
  {"child-count", cfun_node_child_count},
  {"tree", cfun_node_tree},
  {"text", cfun_node_text},
  {NULL, NULL}
};

 int jts_node_get(void* p, Janet key, Janet* out) {
  (void) p;
  if (!janet_checktype(key, JANET_KEYWORD)) {
    return 0;
  }
  return janet_getmethod(janet_unwrap_keyword(key), node_methods, out);
}

////////


static Janet cfun_tree_root_node(int32_t argc, Janet* argv) {
  janet_fixarity(argc, 1);
  // XXX: error checking?
  Tree* tree = janet_getabstract(argv, 0, &jts_tree_type);

  Node* rn =
    (Node *)janet_abstract(&jts_node_type, sizeof(Node));
  // XXX: error checking?

  rn->node = ts_tree_root_node(tree->tree);

  // XXX: is this appropriate checking?
  if (ts_node_is_null(rn->node)) {
    return janet_wrap_nil();
  }

  return janet_wrap_abstract(rn);
}

static const JanetMethod tree_methods[] = {
  // XXX: fix docs
  {"root-node", cfun_tree_root_node},
  {NULL, NULL}
};

static int jts_tree_gc(void *p, size_t size) {
  (void) size;
  Tree *tree= (Tree *)p;
  if (tree) {
    if (NULL != tree->tree) {
      free(tree->tree);
      tree->tree = NULL;
    }
  }
  return 0;
}

static int jts_tree_get(void* p, Janet key, Janet* out) {
  (void) p;
  if (!janet_checktype(key, JANET_KEYWORD)) {
    return 0;
  }
  return janet_getmethod(janet_unwrap_keyword(key), tree_methods, out);
}

////////

/* static Janet cfun_parser_set_language(int32_t argc, Janet* argv) { */

/* } */

static Janet cfun_parser_parse_string(int32_t argc, Janet* argv) {
  janet_arity(argc, 2, 3);
  Parser* parser = janet_getabstract(argv, 0, &jts_parser_type);
  TSParser* tsparser = parser->parser;
  TSTree* tstree = NULL;
  const char* src;
  if (argc == 2) {
    src = (const char *)janet_getstring(argv, 1);
  } else {
    Tree* old_tree = janet_getabstract(argv, 1, &jts_tree_type);
    tstree = old_tree->tree;
    src = (const char *)janet_getstring(argv, 2);
  }

  TSTree* new_tree = ts_parser_parse_string(
    tsparser,
    tstree,
    src,
    strlen(src)
  );

  if (NULL == new_tree) {
    return janet_wrap_nil();
  }

  Tree* tree =
    (Tree *)janet_abstract(&jts_tree_type, sizeof(Tree));

  tree->tree = new_tree;

  return janet_wrap_abstract(tree);
}

static const JanetMethod parser_methods[] = {
  // XXX: fix docs
  //  {"set-language", cfun_parser_set_language},
  //  {"language", cfun_parser_language},
  {"parse-string", cfun_parser_parse_string},
  //  {"set-included-ranges", cfun_parser_set_included_ranges},
  //  {"included-ranges", cfun_parser_included_ranges},
  {NULL, NULL}
};

static int jts_parser_gc(void *p, size_t size) {
  (void) size;
  Parser *parser= (Parser *)p;
  if (parser) {
    if (NULL != parser->parser) {
      free(parser->parser);
      parser->parser = NULL;
    }
  }
  return 0;
}

static int jts_parser_get(void* p, Janet key, Janet* out) {
  (void) p;
  if (!janet_checktype(key, JANET_KEYWORD)) {
    return 0;
  }
  return janet_getmethod(janet_unwrap_keyword(key), parser_methods, out);
}

static const JanetReg cfuns[] = {
  // XXX: fix docs
  {"init", cfun_ts_init,
   "(tree-sitter/init name)\n\n"
   "Return tree-sitter parser for grammar.\n"
   "`name` identifies a grammar, e.g. `janet_simple` or `clojure`.\n"},
  {NULL, NULL, NULL}
};

JANET_MODULE_ENTRY(JanetTable* env) {
  janet_register_abstract_type(&jts_parser_type);
  janet_register_abstract_type(&jts_tree_type);
  janet_register_abstract_type(&jts_node_type);
  janet_cfuns(env, "tree-sitter", cfuns);
}
