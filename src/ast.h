#ifndef SB_AST_H
#define SB_AST_H

#include "lexer.h"

typedef enum {
    NODE_NUMBER_LIT, NODE_STRING_LIT, NODE_BOOL_LIT, NODE_IDENTIFIER,
    NODE_BINARY_OP, NODE_UNARY_OP, NODE_ASSIGN,
    NODE_FUNCTION_DEF, NODE_FUNCTION_CALL, NODE_RETURN,
    NODE_IF, NODE_COUNT, NODE_SAY, NODE_IMPORT,
    NODE_INDEX, NODE_POSSESSIVE,
    NODE_TRY, NODE_RAISE, NODE_LOOP_THROUGH,
    NODE_CLASS_DEF, NODE_NEW_INSTANCE,
    NODE_START_TASK, NODE_WAIT_TASK, NODE_INDEX_ASSIGN,
    NODE_WHILE, NODE_BREAK, NODE_CONTINUE, NODE_MATCH,
    NODE_ASSERT, NODE_LAMBDA,
    NODE_PROGRAM, NODE_BLOCK
} NodeType;

typedef struct AstNode AstNode;

typedef struct {
    AstNode **items;
    int count;
    int capacity;
} NodeList;

typedef struct {
    AstNode *left;
    TokenType op;
    AstNode *right;
} BinaryOp;

typedef struct {
    TokenType op;
    AstNode *operand;
} UnaryOp;

typedef struct {
    char *name;
    AstNode *value;
    char *type_name;
} Assign;

typedef struct {
    char *name;
    char **params;
    int param_count;
    AstNode *body;
} FunctionDef;

typedef struct {
    AstNode *receiver;   // NULL for plain calls, set for method calls
    char *name;
    NodeList args;
} FunctionCall;

typedef struct {
    AstNode *value;
} Return;

typedef struct {
    AstNode *condition;
    AstNode *then_block;
    AstNode *else_block;
} If;

typedef struct {
    char *var_name;
    AstNode *from;
    AstNode *to;
    AstNode *body;
} Count;

typedef struct {
    AstNode *value;
} Say;

typedef struct {
    char *module;
    char *alias;
} Import;

typedef struct {
    AstNode *object;
    char *field;
} Possessive;

typedef struct {
    AstNode *object;
    AstNode *index;
} Index;

typedef struct {
    AstNode *body;
    char *error_var;
    AstNode *catch_body;
} Try;

typedef struct {
    AstNode *message;
} Raise;

typedef struct {
    char *var_name;
    AstNode *iterable;
    AstNode *body;
} LoopThrough;

typedef struct {
    AstNode *condition;
    AstNode *body;
} While;

typedef struct {
    AstNode *pattern;
    AstNode *body;
} MatchCase;

typedef struct {
    AstNode *value;
    MatchCase *cases;
    int case_count;
    int case_capacity;
} Match;

typedef struct {
    AstNode *value;
    AstNode *expected;
    int line;
} Assert;

typedef struct {
    char *name;
    char **fields;
    int field_count;
    NodeList methods;
} ClassDefNode;

typedef struct {
    char *class_name;
    NodeList args;
} NewInstance;

typedef struct {
    AstNode *call;
    char *name;
} StartTask;

typedef struct {
    char *name;
} WaitTask;

typedef struct {
    char *name;
    AstNode *index;
    AstNode *value;
} IndexAssign;

struct AstNode {
    NodeType type;
    int line;
    union {
        double number;
        char *string;
        int boolean;
        char *identifier;
        BinaryOp binary;
        UnaryOp unary;
        Assign assign;
        FunctionDef func_def;
        FunctionCall func_call;
        Return ret;
        If if_stmt;
        Count count;
        Say say;
        Import import;
        Possessive possessive;
        Index index;
        Try try_stmt;
        Raise raise;
        LoopThrough loop_through;
        While while_stmt;
        Match match;
        Assert assert_stmt;
        ClassDefNode class_def;
        NewInstance new_instance;
        StartTask start_task;
        WaitTask wait_task;
        IndexAssign index_assign;
        NodeList program;
        NodeList block;
    } as;
};

NodeList nodelist_create(void);
void nodelist_push(NodeList *list, AstNode *node);
AstNode *node_number(double value, int line);
AstNode *node_string(const char *value, int line);
AstNode *node_bool(int value, int line);
AstNode *node_identifier(const char *name, int line);
AstNode *node_binary(AstNode *left, TokenType op, AstNode *right, int line);
AstNode *node_unary(TokenType op, AstNode *operand, int line);
AstNode *node_assign(const char *name, AstNode *value, const char *type_name, int line);
AstNode *node_func_def(const char *name, char **params, int param_count, AstNode *body, int line);
AstNode *node_func_call(const char *name, NodeList args, int line);
AstNode *node_method_call(AstNode *receiver, const char *name, NodeList args, int line);
AstNode *node_return(AstNode *value, int line);
AstNode *node_if(AstNode *cond, AstNode *then_b, AstNode *else_b, int line);
AstNode *node_count(const char *var, AstNode *from, AstNode *to, AstNode *body, int line);
AstNode *node_say(AstNode *value, int line);
AstNode *node_import(const char *module, const char *alias, int line);
AstNode *node_possessive(AstNode *object, const char *field, int line);
AstNode *node_index(AstNode *object, AstNode *index, int line);
AstNode *node_try(AstNode *body, const char *error_var, AstNode *catch_body, int line);
AstNode *node_raise(AstNode *message, int line);
AstNode *node_loop_through(const char *var, AstNode *iterable, AstNode *body, int line);
AstNode *node_class_def(const char *name, char **fields, int field_count, NodeList methods, int line);
AstNode *node_new_instance(const char *class_name, NodeList args, int line);
AstNode *node_start_task(AstNode *call, const char *name, int line);
AstNode *node_wait_task(const char *name, int line);
AstNode *node_index_assign(const char *name, AstNode *index, AstNode *value, int line);
AstNode *node_while(AstNode *cond, AstNode *body, int line);
AstNode *node_break(int line);
AstNode *node_continue(int line);
AstNode *node_match(AstNode *value, int line);
void node_match_add_case(AstNode *match, AstNode *pattern, AstNode *body);
AstNode *node_assert(AstNode *value, AstNode *expected, int line);
AstNode *node_lambda(char **params, int param_count, AstNode *body, int line);
AstNode *node_program(NodeList stmts);
AstNode *node_block(NodeList stmts);
void node_free(AstNode *node);

#endif
