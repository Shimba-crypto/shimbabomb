#ifndef SB_INTERPRETER_H
#define SB_INTERPRETER_H

#include "ast.h"
#include "value.h"
#include <sys/types.h>

#ifndef SB_STD_DIR
#define SB_STD_DIR "std"
#endif

#define SB_MAX_SEARCH_PATHS 16
#define SB_MAX_LOADED_MODULES 64
#define SB_MAX_TASKS 64
#define SB_MAX_CALLS 32
#define SB_MAX_RECURSION 2000
#define SB_MAX_EMBED 64

typedef struct EnvBinding {
    char *name;
    Value value;
} EnvBinding;

struct Environment {
    EnvBinding *bindings;
    int count;
    int capacity;
    Environment *parent;
};

typedef enum {
    ERR_NONE = 0,
    ERR_TYPE_MISMATCH,
    ERR_DIV_ZERO,
    ERR_OUT_OF_BOUNDS,
    ERR_NOT_FOUND,
    ERR_RUNTIME
} ErrorCode;

typedef struct {
    Environment *global;
    int had_error;
    ErrorCode error_code;
    char error_msg[512];
    int error_line;

    int returning;
    Value return_value;
    int breaking;
    int continuing;

    char *search_paths[SB_MAX_SEARCH_PATHS];
    int search_path_count;

    char *loaded[SB_MAX_LOADED_MODULES];
    int loaded_count;

    char *task_names[SB_MAX_TASKS];
    pid_t task_pids[SB_MAX_TASKS];
    int task_count;

    int asserts_passed;
    int asserts_failed;
    int debug;

    char call_stack[SB_MAX_CALLS][64];
    int call_lines[SB_MAX_CALLS];
    int call_depth;
    int recursion_depth;
    char error_trace[1024];

    char *embed_names[SB_MAX_EMBED];
    char *embed_srcs[SB_MAX_EMBED];
    int embed_count;

    char main_path[1024];
} Interpreter;

void interp_embed_module(Interpreter *interp, const char *name, const char *src);

void interp_init(Interpreter *interp);
void interp_free(Interpreter *interp);
Value interp_run(Interpreter *interp, AstNode *program);
void interp_add_search_path(Interpreter *interp, const char *dir);

#endif
