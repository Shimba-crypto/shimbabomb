#define _POSIX_C_SOURCE 200809L
#include "ast.h"
#include <stdlib.h>
#include <string.h>

NodeList nodelist_create(void) {
    NodeList l = {NULL, 0, 0};
    return l;
}

void nodelist_push(NodeList *list, AstNode *node) {
    if (list->count >= list->capacity) {
        list->capacity = list->capacity == 0 ? 8 : list->capacity * 2;
        list->items = realloc(list->items, sizeof(AstNode*) * list->capacity);
    }
    list->items[list->count++] = node;
}

AstNode *node_number(double value, int line) {
    AstNode *n = calloc(1, sizeof(AstNode));
    n->type = NODE_NUMBER_LIT; n->line = line; n->as.number = value;
    return n;
}
AstNode *node_string(const char *value, int line) {
    AstNode *n = calloc(1, sizeof(AstNode));
    n->type = NODE_STRING_LIT; n->line = line; n->as.string = strdup(value);
    return n;
}
AstNode *node_bool(int value, int line) {
    AstNode *n = calloc(1, sizeof(AstNode));
    n->type = NODE_BOOL_LIT; n->line = line; n->as.boolean = value;
    return n;
}
AstNode *node_identifier(const char *name, int line) {
    AstNode *n = calloc(1, sizeof(AstNode));
    n->type = NODE_IDENTIFIER; n->line = line; n->as.identifier = strdup(name);
    return n;
}
AstNode *node_binary(AstNode *left, TokenType op, AstNode *right, int line) {
    AstNode *n = calloc(1, sizeof(AstNode));
    n->type = NODE_BINARY_OP; n->line = line;
    n->as.binary.left = left; n->as.binary.op = op; n->as.binary.right = right;
    return n;
}
AstNode *node_unary(TokenType op, AstNode *operand, int line) {
    AstNode *n = calloc(1, sizeof(AstNode));
    n->type = NODE_UNARY_OP; n->line = line;
    n->as.unary.op = op; n->as.unary.operand = operand;
    return n;
}
AstNode *node_assign(const char *name, AstNode *value, const char *type_name, int line) {
    AstNode *n = calloc(1, sizeof(AstNode));
    n->type = NODE_ASSIGN; n->line = line;
    n->as.assign.name = strdup(name); n->as.assign.value = value;
    n->as.assign.type_name = type_name ? strdup(type_name) : NULL;
    return n;
}
AstNode *node_func_def(const char *name, char **params, int param_count, AstNode *body, int line) {
    AstNode *n = calloc(1, sizeof(AstNode));
    n->type = NODE_FUNCTION_DEF; n->line = line;
    n->as.func_def.name = strdup(name);
    n->as.func_def.params = params;
    n->as.func_def.param_count = param_count;
    n->as.func_def.body = body;
    return n;
}
AstNode *node_func_call(const char *name, NodeList args, int line) {
    AstNode *n = calloc(1, sizeof(AstNode));
    n->type = NODE_FUNCTION_CALL; n->line = line;
    n->as.func_call.receiver = NULL;
    n->as.func_call.name = strdup(name);
    n->as.func_call.args = args;
    return n;
}
AstNode *node_method_call(AstNode *receiver, const char *name, NodeList args, int line) {
    AstNode *n = calloc(1, sizeof(AstNode));
    n->type = NODE_FUNCTION_CALL; n->line = line;
    n->as.func_call.receiver = receiver;
    n->as.func_call.name = strdup(name);
    n->as.func_call.args = args;
    return n;
}
AstNode *node_return(AstNode *value, int line) {
    AstNode *n = calloc(1, sizeof(AstNode));
    n->type = NODE_RETURN; n->line = line; n->as.ret.value = value;
    return n;
}
AstNode *node_if(AstNode *cond, AstNode *then_b, AstNode *else_b, int line) {
    AstNode *n = calloc(1, sizeof(AstNode));
    n->type = NODE_IF; n->line = line;
    n->as.if_stmt.condition = cond;
    n->as.if_stmt.then_block = then_b;
    n->as.if_stmt.else_block = else_b;
    return n;
}
AstNode *node_count(const char *var, AstNode *from, AstNode *to, AstNode *body, int line) {
    AstNode *n = calloc(1, sizeof(AstNode));
    n->type = NODE_COUNT; n->line = line;
    n->as.count.var_name = strdup(var);
    n->as.count.from = from; n->as.count.to = to; n->as.count.body = body;
    return n;
}
AstNode *node_say(AstNode *value, int line) {
    AstNode *n = calloc(1, sizeof(AstNode));
    n->type = NODE_SAY; n->line = line; n->as.say.value = value;
    return n;
}
AstNode *node_import(const char *module, const char *alias, int line) {
    AstNode *n = calloc(1, sizeof(AstNode));
    n->type = NODE_IMPORT; n->line = line; n->as.import.module = strdup(module);
    n->as.import.alias = alias ? strdup(alias) : NULL;
    return n;
}
AstNode *node_possessive(AstNode *object, const char *field, int line) {
    AstNode *n = calloc(1, sizeof(AstNode));
    n->type = NODE_POSSESSIVE; n->line = line;
    n->as.possessive.object = object; n->as.possessive.field = strdup(field);
    return n;
}
AstNode *node_index(AstNode *object, AstNode *index, int line) {
    AstNode *n = calloc(1, sizeof(AstNode));
    n->type = NODE_INDEX; n->line = line;
    n->as.index.object = object; n->as.index.index = index;
    return n;
}
AstNode *node_try(AstNode *body, const char *error_var, AstNode *catch_body, int line) {
    AstNode *n = calloc(1, sizeof(AstNode));
    n->type = NODE_TRY; n->line = line;
    n->as.try_stmt.body = body;
    n->as.try_stmt.error_var = strdup(error_var);
    n->as.try_stmt.catch_body = catch_body;
    return n;
}
AstNode *node_raise(AstNode *message, int line) {
    AstNode *n = calloc(1, sizeof(AstNode));
    n->type = NODE_RAISE; n->line = line; n->as.raise.message = message;
    return n;
}
AstNode *node_loop_through(const char *var, AstNode *iterable, AstNode *body, int line) {
    AstNode *n = calloc(1, sizeof(AstNode));
    n->type = NODE_LOOP_THROUGH; n->line = line;
    n->as.loop_through.var_name = strdup(var);
    n->as.loop_through.iterable = iterable;
    n->as.loop_through.body = body;
    return n;
}
AstNode *node_class_def(const char *name, char **fields, int field_count, NodeList methods, int line) {
    AstNode *n = calloc(1, sizeof(AstNode));
    n->type = NODE_CLASS_DEF; n->line = line;
    n->as.class_def.name = strdup(name);
    n->as.class_def.fields = fields;
    n->as.class_def.field_count = field_count;
    n->as.class_def.methods = methods;
    return n;
}
AstNode *node_new_instance(const char *class_name, NodeList args, int line) {
    AstNode *n = calloc(1, sizeof(AstNode));
    n->type = NODE_NEW_INSTANCE; n->line = line;
    n->as.new_instance.class_name = strdup(class_name);
    n->as.new_instance.args = args;
    return n;
}
AstNode *node_start_task(AstNode *call, const char *name, int line) {
    AstNode *n = calloc(1, sizeof(AstNode));
    n->type = NODE_START_TASK; n->line = line;
    n->as.start_task.call = call;
    n->as.start_task.name = strdup(name);
    return n;
}
AstNode *node_wait_task(const char *name, int line) {
    AstNode *n = calloc(1, sizeof(AstNode));
    n->type = NODE_WAIT_TASK; n->line = line;
    n->as.wait_task.name = strdup(name);
    return n;
}
AstNode *node_index_assign(const char *name, AstNode *index, AstNode *value, int line) {
    AstNode *n = calloc(1, sizeof(AstNode));
    n->type = NODE_INDEX_ASSIGN; n->line = line;
    n->as.index_assign.name = strdup(name);
    n->as.index_assign.index = index;
    n->as.index_assign.value = value;
    return n;
}
AstNode *node_while(AstNode *cond, AstNode *body, int line) {
    AstNode *n = calloc(1, sizeof(AstNode));
    n->type = NODE_WHILE; n->line = line;
    n->as.while_stmt.condition = cond;
    n->as.while_stmt.body = body;
    return n;
}
AstNode *node_break(int line) {
    AstNode *n = calloc(1, sizeof(AstNode));
    n->type = NODE_BREAK; n->line = line;
    return n;
}
AstNode *node_continue(int line) {
    AstNode *n = calloc(1, sizeof(AstNode));
    n->type = NODE_CONTINUE; n->line = line;
    return n;
}
AstNode *node_match(AstNode *value, int line) {
    AstNode *n = calloc(1, sizeof(AstNode));
    n->type = NODE_MATCH; n->line = line;
    n->as.match.value = value;
    n->as.match.cases = NULL;
    n->as.match.case_count = 0;
    n->as.match.case_capacity = 0;
    return n;
}
void node_match_add_case(AstNode *match, AstNode *pattern, AstNode *body) {
    if (match->as.match.case_count >= match->as.match.case_capacity) {
        match->as.match.case_capacity = match->as.match.case_capacity==0?4:match->as.match.case_capacity*2;
        match->as.match.cases = realloc(match->as.match.cases, sizeof(MatchCase)*match->as.match.case_capacity);
    }
    match->as.match.cases[match->as.match.case_count].pattern = pattern;
    match->as.match.cases[match->as.match.case_count].body = body;
    match->as.match.case_count++;
}
AstNode *node_assert(AstNode *value, AstNode *expected, int line) {
    AstNode *n = calloc(1, sizeof(AstNode));
    n->type = NODE_ASSERT; n->line = line;
    n->as.assert_stmt.value = value;
    n->as.assert_stmt.expected = expected;
    return n;
}
AstNode *node_lambda(char **params, int param_count, AstNode *body, int line) {
    AstNode *n = calloc(1, sizeof(AstNode));
    n->type = NODE_LAMBDA; n->line = line;
    n->as.func_def.name = NULL;
    n->as.func_def.params = params;
    n->as.func_def.param_count = param_count;
    n->as.func_def.body = body;
    return n;
}
AstNode *node_program(NodeList stmts) {
    AstNode *n = calloc(1, sizeof(AstNode));
    n->type = NODE_PROGRAM; n->line = 1; n->as.program = stmts;
    return n;
}
AstNode *node_block(NodeList stmts) {
    AstNode *n = calloc(1, sizeof(AstNode));
    n->type = NODE_BLOCK; n->line = 1; n->as.block = stmts;
    return n;
}

static void free_nodelist(NodeList *l) {
    for (int i = 0; i < l->count; i++) node_free(l->items[i]);
    free(l->items);
}

void node_free(AstNode *node) {
    if (!node) return;
    switch (node->type) {
        case NODE_STRING_LIT: case NODE_IDENTIFIER:
            free(node->as.string); break;
        case NODE_BINARY_OP:
            node_free(node->as.binary.left);
            node_free(node->as.binary.right); break;
        case NODE_UNARY_OP:
            node_free(node->as.unary.operand); break;
        case NODE_ASSIGN:
            free(node->as.assign.name);
            free(node->as.assign.type_name);
            node_free(node->as.assign.value); break;
        case NODE_LAMBDA:
        case NODE_FUNCTION_DEF:
            free(node->as.func_def.name);
            for (int i = 0; i < node->as.func_def.param_count; i++)
                free(node->as.func_def.params[i]);
            free(node->as.func_def.params);
            node_free(node->as.func_def.body); break;
        case NODE_FUNCTION_CALL:
            node_free(node->as.func_call.receiver);
            free(node->as.func_call.name);
            free_nodelist(&node->as.func_call.args); break;
        case NODE_RETURN: node_free(node->as.ret.value); break;
        case NODE_IF:
            node_free(node->as.if_stmt.condition);
            node_free(node->as.if_stmt.then_block);
            node_free(node->as.if_stmt.else_block); break;
        case NODE_COUNT:
            free(node->as.count.var_name);
            node_free(node->as.count.from);
            node_free(node->as.count.to);
            node_free(node->as.count.body); break;
        case NODE_SAY: node_free(node->as.say.value); break;
        case NODE_IMPORT: free(node->as.import.module); free(node->as.import.alias); break;
        case NODE_POSSESSIVE:
            node_free(node->as.possessive.object);
            free(node->as.possessive.field); break;
        case NODE_INDEX:
            node_free(node->as.index.object);
            node_free(node->as.index.index); break;
        case NODE_TRY:
            node_free(node->as.try_stmt.body);
            free(node->as.try_stmt.error_var);
            node_free(node->as.try_stmt.catch_body); break;
        case NODE_RAISE: node_free(node->as.raise.message); break;
        case NODE_LOOP_THROUGH:
            free(node->as.loop_through.var_name);
            node_free(node->as.loop_through.iterable);
            node_free(node->as.loop_through.body); break;
        case NODE_CLASS_DEF:
            free(node->as.class_def.name);
            for (int i = 0; i < node->as.class_def.field_count; i++)
                free(node->as.class_def.fields[i]);
            free(node->as.class_def.fields);
            free_nodelist(&node->as.class_def.methods); break;
        case NODE_NEW_INSTANCE:
            free(node->as.new_instance.class_name);
            free_nodelist(&node->as.new_instance.args); break;
        case NODE_START_TASK:
            node_free(node->as.start_task.call);
            free(node->as.start_task.name); break;
        case NODE_WAIT_TASK:
            free(node->as.wait_task.name); break;
        case NODE_INDEX_ASSIGN:
            free(node->as.index_assign.name);
            node_free(node->as.index_assign.index);
            node_free(node->as.index_assign.value); break;
        case NODE_WHILE:
            node_free(node->as.while_stmt.condition);
            node_free(node->as.while_stmt.body); break;
        case NODE_BREAK: case NODE_CONTINUE: break;
        case NODE_MATCH:
            node_free(node->as.match.value);
            for (int i=0;i<node->as.match.case_count;i++) {
                node_free(node->as.match.cases[i].pattern);
                node_free(node->as.match.cases[i].body);
            }
            free(node->as.match.cases); break;
        case NODE_ASSERT:
            node_free(node->as.assert_stmt.value);
            node_free(node->as.assert_stmt.expected); break;
        case NODE_PROGRAM: free_nodelist(&node->as.program); break;
        case NODE_BLOCK: free_nodelist(&node->as.block); break;
        default: break;
    }
    free(node);
}
