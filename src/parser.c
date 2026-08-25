#define _POSIX_C_SOURCE 200809L
#include "parser.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static void parser_advance(Parser *p) {
    p->previous = p->current;
    p->current = lexer_next_token(&p->lexer);
}
static int parser_check(Parser *p, TokenType type) { return p->current.type == type; }
static int parser_match(Parser *p, TokenType type) {
    if (parser_check(p, type)) { parser_advance(p); return 1; }
    return 0;
}
static int parser_had_error(Parser *p) { return p->had_error; }
static void parser_error(Parser *p, const char *msg) {
    if (!p->had_error) {
        p->had_error = 1;
        snprintf(p->error_msg, sizeof(p->error_msg), "line %d: %s", p->current.line, msg);
    }
}
static char *copy_token(Parser *p) {
    char *s = malloc(p->current.length + 1);
    memcpy(s, p->current.start, p->current.length);
    s[p->current.length] = '\0';
    return s;
}
static char *parser_expect(Parser *p, TokenType type, const char *msg) {
    if (p->current.type == type) {
        char *s = copy_token(p);
        parser_advance(p);
        return s;
    }
    parser_error(p, msg);
    return strdup("");
}

static AstNode *parse_statement(Parser *p);
static AstNode *parse_expression(Parser *p);
static AstNode *parse_block(Parser *p);
static AstNode *parse_arg_expr(Parser *p);
static AstNode *parse_postfix(Parser *p);

typedef struct { Lexer lex; Token cur; Token prev; } ParserSnap;
static void parser_snap(Parser *p, ParserSnap *s) { s->lex=p->lexer; s->cur=p->current; s->prev=p->previous; }
static void parser_restore(Parser *p, ParserSnap *s) { p->lexer=s->lex; p->current=s->cur; p->previous=s->prev; }

static int starts_call(Parser *p) {
    if (!parser_check(p, TOKEN_IDENTIFIER)) return 0;
    ParserSnap s;
    parser_snap(p, &s);
    parser_advance(p);
    int r = parser_check(p, TOKEN_WITH);
    parser_restore(p, &s);
    return r;
}

static int parse_params(Parser *p, char ***out) {
    int cap = 0, count = 0;
    char **params = NULL;
    if (parser_match(p, TOKEN_WITH)) {
        if (parser_check(p, TOKEN_AND)) parser_advance(p);
        while (parser_check(p, TOKEN_IDENTIFIER)) {
            if (count >= cap) {
                cap = cap == 0 ? 4 : cap * 2;
                params = realloc(params, sizeof(char*) * cap);
            }
            params[count++] = copy_token(p);
            parser_advance(p);
            if (!parser_match(p, TOKEN_AND)) break;
        }
    }
    *out = params;
    return count;
}

static NodeList parse_call_args(Parser *p) {
    NodeList args = nodelist_create();
    if (parser_match(p, TOKEN_WITH)) {
        if (parser_check(p, TOKEN_DOT) || parser_check(p, TOKEN_END) ||
            parser_check(p, TOKEN_CATCH) || parser_check(p, TOKEN_OTHERWISE) ||
            parser_check(p, TOKEN_EOF)) {
            return args;
        }
        if (parser_check(p, TOKEN_AND)) parser_advance(p);
        nodelist_push(&args, parse_arg_expr(p));
        for (;;) {
            if (!parser_match(p, TOKEN_AND)) break;
            nodelist_push(&args, parse_arg_expr(p));
        }
    }
    return args;
}

static AstNode *parse_primary(Parser *p) {
    if (parser_check(p, TOKEN_NUMBER)) {
        char *end;
        double val = strtod(p->current.start, &end);
        int line = p->current.line;
        parser_advance(p);
        return node_number(val, line);
    }
    if (parser_check(p, TOKEN_STRING)) {
        int line = p->current.line;
        int raw_len = p->current.length - 2;
        char *s = malloc(raw_len + 1);
        int j = 0;
        for (int i = 1; i < p->current.length - 1; i++) {
            char c = p->current.start[i];
            if (c == '\\' && i + 1 < p->current.length - 1) {
                i++; c = p->current.start[i];
                if (c == 'n') s[j++] = '\n';
                else if (c == 't') s[j++] = '\t';
                else if (c == 'r') s[j++] = '\r';
                else s[j++] = c;
            } else s[j++] = c;
        }
        s[j] = '\0';
        parser_advance(p);

        // interpolation: "a${expr}b${expr2}c" -> ((("a" + expr) + expr2) + "c")
        if (strstr(s, "${") == NULL) {
            AstNode *n = node_string(s, line);
            free(s);
            return n;
        }
        AstNode *acc = NULL;
        int pos = 0, slen = strlen(s);
        while (pos < slen) {
            char *open = strstr(s + pos, "${");
            if (!open) {
                if (acc) {
                    AstNode *lit = node_string(s + pos, line);
                    acc = node_binary(acc, TOKEN_PLUS, lit, line);
                }
                break;
            }
            int lit_len = (int)(open - (s + pos));
            if (lit_len > 0 || acc == NULL) {
                char *lit = malloc(lit_len + 1);
                memcpy(lit, s + pos, lit_len); lit[lit_len] = '\0';
                AstNode *litn = node_string(lit, line);
                free(lit);
                acc = acc ? node_binary(acc, TOKEN_PLUS, litn, line) : litn;
            }
            char *close = strchr(open + 2, '}');
            if (!close) {  // unterminated: treat rest as literal
                AstNode *litn = node_string(open, line);
                acc = acc ? node_binary(acc, TOKEN_PLUS, litn, line) : litn;
                break;
            }
            int expr_len = (int)(close - (open + 2));
            char *expr_src = malloc(expr_len + 1);
            memcpy(expr_src, open + 2, expr_len); expr_src[expr_len] = '\0';
            Parser sub;
            parser_init(&sub, expr_src);
            AstNode *expr = parse_expression(&sub);
            if (acc) acc = node_binary(acc, TOKEN_PLUS, expr, line);
            else acc = expr;
            free(expr_src);
            pos = (int)(close - s) + 1;
        }
        free(s);
        if (!acc) return node_string("", line);
        return acc;
    }
    if (parser_check(p, TOKEN_TRUE)) { int l = p->current.line; parser_advance(p); return node_bool(1, l); }
    if (parser_check(p, TOKEN_FALSE)) { int l = p->current.line; parser_advance(p); return node_bool(0, l); }
    if (parser_check(p, TOKEN_SELF)) {
        int line = p->current.line;
        parser_advance(p);
        return node_identifier("self", line);
    }
    if (parser_check(p, TOKEN_LPAREN)) {
        parser_advance(p);
        AstNode *expr = parse_expression(p);
        parser_match(p, TOKEN_RPAREN);
        return expr;
    }
    if (parser_check(p, TOKEN_FN)) {
        int line = p->current.line;
        parser_advance(p);
        char **params = NULL;
        int pc = parse_params(p, &params);
        parser_expect(p, TOKEN_AS, "expected 'as' after fn params");
        AstNode *body = parse_block(p);
        parser_expect(p, TOKEN_END, "expected 'end' after fn body");
        return node_lambda(params, pc, body, line);
    }
    if (parser_check(p, TOKEN_NEW)) {
        int line = p->current.line;
        parser_advance(p);
        char *cls = parser_expect(p, TOKEN_IDENTIFIER, "expected class name after 'new'");
        NodeList args = nodelist_create();
        if (parser_match(p, TOKEN_WITH)) {
            if (parser_check(p, TOKEN_AND)) parser_advance(p);
            nodelist_push(&args, parse_arg_expr(p));
            while (parser_match(p, TOKEN_AND))
                nodelist_push(&args, parse_arg_expr(p));
        }
        AstNode *n = node_new_instance(cls, args, line);
        free(cls);
        return n;
    }
    if (parser_check(p, TOKEN_WAIT) || parser_check(p, TOKEN_AWAIT)) {
        int line = p->current.line;
        parser_advance(p);
        if (parser_check(p, TOKEN_IDENTIFIER) && p->current.length == 3 &&
            memcmp(p->current.start, "for", 3) == 0) {
            parser_advance(p);
        }
        char *name = parser_expect(p, TOKEN_IDENTIFIER, "expected task name after 'wait'");
        AstNode *n = node_wait_task(name, line);
        free(name);
        return n;
    }
    if (parser_check(p, TOKEN_IDENTIFIER)) {
        int line = p->current.line;
        char *name = copy_token(p);
        parser_advance(p);
        if (parser_check(p, TOKEN_WITH)) {
            NodeList args = parse_call_args(p);
            AstNode *n = node_func_call(name, args, line);
            free(name);
            return n;
        }
        if (parser_check(p, TOKEN_LPAREN)) {
            parser_advance(p);
            NodeList args = nodelist_create();
            if (!parser_check(p, TOKEN_RPAREN)) {
                nodelist_push(&args, parse_expression(p));
                while (parser_check(p, TOKEN_COMMA) || parser_check(p, TOKEN_AND)) {
                    parser_advance(p);
                    nodelist_push(&args, parse_expression(p));
                }
            }
            parser_match(p, TOKEN_RPAREN);
            AstNode *n = node_func_call(name, args, line);
            free(name);
            return n;
        }
        AstNode *n = node_identifier(name, line);
        free(name);
        return n;
    }

    parser_error(p, "expected an expression");
    int line = p->current.line;
    if (!parser_check(p, TOKEN_EOF)) parser_advance(p);
    return node_number(0, line);
}

static AstNode *parse_unary(Parser *p) {
    if (parser_check(p, TOKEN_NOT)) {
        int line = p->current.line;
        parser_advance(p);
        return node_unary(TOKEN_NOT, parse_unary(p), line);
    }
    if (parser_check(p, TOKEN_MINUS)) {
        int line = p->current.line;
        parser_advance(p);
        return node_unary(TOKEN_MINUS, parse_unary(p), line);
    }
    return parse_primary(p);
}

static AstNode *parse_postfix(Parser *p);

static AstNode *parse_term(Parser *p) {
    AstNode *left = parse_postfix(p);
    for (;;) {
        TokenType op = p->current.type;
        if (op != TOKEN_STAR && op != TOKEN_SLASH && op != TOKEN_MOD && op != TOKEN_INTDIV) break;
        int line = p->current.line;
        parser_advance(p);
        AstNode *right = parse_postfix(p);
        left = node_binary(left, op, right, line);
    }
    return left;
}

static AstNode *parse_arg_term(Parser *p) {
    AstNode *left = parse_postfix(p);
    for (;;) {
        TokenType op = p->current.type;
        if (op != TOKEN_STAR && op != TOKEN_SLASH) break;
        ParserSnap snap;
        parser_snap(p, &snap);
        int line = p->current.line;
        parser_advance(p);
        if (starts_call(p)) { parser_restore(p, &snap); break; }
        AstNode *right = parse_arg_term(p);
        left = node_binary(left, op, right, line);
    }
    return left;
}

static AstNode *parse_arg_expr(Parser *p) {
    AstNode *left = parse_arg_term(p);
    for (;;) {
        TokenType op = p->current.type;
        if (op != TOKEN_PLUS && op != TOKEN_MINUS) break;
        ParserSnap snap;
        parser_snap(p, &snap);
        int line = p->current.line;
        parser_advance(p);
        if (starts_call(p)) { parser_restore(p, &snap); break; }
        AstNode *right = parse_arg_expr(p);
        left = node_binary(left, op, right, line);
    }
    return left;
}

static AstNode *parse_additive(Parser *p);

static AstNode *parse_comparison(Parser *p) {
    AstNode *left = parse_term(p);
    if (parser_had_error(p)) return left;

    if (parser_check(p, TOKEN_NOTEQ)) {
        int line = p->current.line;
        parser_advance(p);
        AstNode *right = parse_additive(p);
        return node_binary(left, TOKEN_NOTEQ, right, line);
    }
    if (parser_check(p, TOKEN_LESS) || parser_check(p, TOKEN_GREATER) ||
        parser_check(p, TOKEN_EQUAL)) {
        TokenType op = p->current.type;
        int line = p->current.line;
        parser_advance(p);
        AstNode *right = parse_additive(p);
        return node_binary(left, op, right, line);
    }
    if (parser_check(p, TOKEN_IS)) {
        int line = p->current.line;
        parser_advance(p);
        AstNode *right = parse_additive(p);
        return node_binary(left, TOKEN_EQUAL, right, line);
    }
    // no comparison — continue additive chain from left
    for (;;) {
        TokenType op = p->current.type;
        if (op != TOKEN_PLUS && op != TOKEN_MINUS) break;
        int line = p->current.line;
        parser_advance(p);
        AstNode *right = parse_term(p);
        left = node_binary(left, op, right, line);
    }
    return left;
}

static AstNode *parse_additive(Parser *p) {
    AstNode *left = parse_term(p);
    for (;;) {
        TokenType op = p->current.type;
        if (op != TOKEN_PLUS && op != TOKEN_MINUS) break;
        int line = p->current.line;
        parser_advance(p);
        AstNode *right = parse_term(p);
        left = node_binary(left, op, right, line);
    }
    return left;
}

static AstNode *parse_postfix(Parser *p) {
    AstNode *expr = parse_unary(p);

    for (;;) {
        if (parser_check(p, TOKEN_PSS)) {
            int line = p->current.line;
            parser_advance(p);
            char *field = parser_expect(p, TOKEN_IDENTIFIER, "expected name after 's");
            if (p->had_error) return expr;
            if (parser_check(p, TOKEN_WITH)) {
                NodeList args = parse_call_args(p);
                expr = node_method_call(expr, field, args, line);
            } else {
                expr = node_possessive(expr, field, line);
            }
            free(field);
            continue;
        }
        if (parser_check(p, TOKEN_LBRACKET)) {
            int line = p->current.line;
            parser_advance(p);
            AstNode *idx = parse_expression(p);
            parser_expect(p, TOKEN_RBRACKET, "expected ']'");
            expr = node_index(expr, idx, line);
            continue;
        }
        break;
    }
    return expr;
}

static AstNode *parse_condition(Parser *p) {
    AstNode *left = parse_comparison(p);
    for (;;) {
        if (parser_check(p, TOKEN_OR)) {
            int line = p->current.line;
            parser_advance(p);
            AstNode *right = parse_comparison(p);
            left = node_binary(left, TOKEN_OR, right, line);
            continue;
        }
        if (parser_check(p, TOKEN_AND)) {
            int line = p->current.line;
            parser_advance(p);
            AstNode *right = parse_comparison(p);
            left = node_binary(left, TOKEN_AND, right, line);
            continue;
        }
        break;
    }
    return left;
}

static AstNode *parse_expression(Parser *p) {
    return parse_comparison(p);
}

static AstNode *parse_block(Parser *p) {
    NodeList stmts = nodelist_create();
    while (!parser_check(p, TOKEN_END) && !parser_check(p, TOKEN_OTHERWISE) &&
           !parser_check(p, TOKEN_CATCH) && !parser_check(p, TOKEN_EOF) &&
           !p->had_error) {
        AstNode *stmt = parse_statement(p);
        if (stmt) nodelist_push(&stmts, stmt);
        parser_match(p, TOKEN_DOT);
    }
    return node_block(stmts);
}

static AstNode *parse_statement(Parser *p) {
    if (parser_check(p, TOKEN_PLS)) {
        int line = p->current.line;
        parser_advance(p);
        parser_expect(p, TOKEN_BRING, "expected 'bring' after 'pls'");
        if (p->had_error) return node_number(0, line);
        char *mod = NULL;
        if (parser_check(p, TOKEN_STRING)) {
            mod = malloc(p->current.length - 1);
            memcpy(mod, p->current.start + 1, p->current.length - 2);
            mod[p->current.length - 2] = '\0';
            parser_advance(p);
        } else if (parser_check(p, TOKEN_IDENTIFIER)) {
            mod = copy_token(p);
            parser_advance(p);
        } else {
            parser_error(p, "expected a module name after 'pls bring'");
            return node_number(0, line);
        }
        char *alias = NULL;
        if (parser_match(p, TOKEN_AS)) {
            alias = parser_expect(p, TOKEN_IDENTIFIER, "expected alias after 'as'");
        }
        AstNode *n = node_import(mod, alias, line);
        free(mod); free(alias);
        return n;
    }

    if (parser_check(p, TOKEN_SAY)) {
        int line = p->current.line;
        parser_advance(p);
        return node_say(parse_expression(p), line);
    }

    if (parser_check(p, TOKEN_SET)) {
        int line = p->current.line;
        parser_advance(p);
        char *name = parser_expect(p, TOKEN_IDENTIFIER, "expected variable name");
        if (p->had_error) return node_number(0, line);
        if (parser_check(p, TOKEN_LBRACKET)) {
            parser_advance(p);
            AstNode *idx = parse_expression(p);
            parser_expect(p, TOKEN_RBRACKET, "expected ']'");
            parser_expect(p, TOKEN_TO, "expected 'to'");
            AstNode *value = parse_expression(p);
            AstNode *n = node_index_assign(name, idx, value, line);
            free(name);
            return n;
        }
        parser_expect(p, TOKEN_TO, "expected 'to'");
        AstNode *value = parse_expression(p);
        char *type_name = NULL;
        if (parser_match(p, TOKEN_AS)) {
            type_name = parser_expect(p, TOKEN_IDENTIFIER, "expected type name after 'as'");
        }
        AstNode *n = node_assign(name, value, type_name, line);
        free(name); free(type_name);
        return n;
    }

    if (parser_check(p, TOKEN_DEFINE)) {
        int line = p->current.line;
        parser_advance(p);
        char *name = parser_expect(p, TOKEN_IDENTIFIER, "expected function name");
        char **params;
        int pc = parse_params(p, &params);
        parser_expect(p, TOKEN_AS, "expected 'as'");
        AstNode *body = parse_block(p);
        parser_expect(p, TOKEN_END, "expected 'end'");
        AstNode *n = node_func_def(name, params, pc, body, line);
        free(name);
        return n;
    }

    if (parser_check(p, TOKEN_CLASS)) {
        int line = p->current.line;
        parser_advance(p);
        char *name = parser_expect(p, TOKEN_IDENTIFIER, "expected class name");
        char **fields = NULL;
        int fc = 0, cap = 0;
        if (parser_match(p, TOKEN_WITH)) {
            if (parser_check(p, TOKEN_AND)) parser_advance(p);
            while (parser_check(p, TOKEN_IDENTIFIER)) {
                if (fc >= cap) {
                    cap = cap == 0 ? 4 : cap * 2;
                    fields = realloc(fields, sizeof(char*) * cap);
                }
                fields[fc++] = copy_token(p);
                parser_advance(p);
                if (!parser_match(p, TOKEN_AND)) break;
            }
        }
        NodeList methods = nodelist_create();
        while (parser_check(p, TOKEN_METHOD)) {
            int ml = p->current.line;
            parser_advance(p);
            char *mname = parser_expect(p, TOKEN_IDENTIFIER, "expected method name");
            char **mp;
            int mpc = parse_params(p, &mp);
            parser_expect(p, TOKEN_AS, "expected 'as'");
            AstNode *mb = parse_block(p);
            parser_expect(p, TOKEN_END, "expected 'end' after method");
            nodelist_push(&methods, node_func_def(mname, mp, mpc, mb, ml));
            free(mname);
        }
        parser_expect(p, TOKEN_END, "expected 'end' after class");
        AstNode *n = node_class_def(name, fields, fc, methods, line);
        free(name);
        return n;
    }

    if (parser_check(p, TOKEN_GIVE)) {
        int line = p->current.line;
        parser_advance(p);
        parser_expect(p, TOKEN_BACK, "expected 'back' after 'give'");
        return node_return(parse_expression(p), line);
    }

    if (parser_check(p, TOKEN_IF)) {
        int line = p->current.line;
        parser_advance(p);
        AstNode *cond = parse_condition(p);
        parser_expect(p, TOKEN_THEN, "expected 'then'");
        AstNode *then_b = parse_block(p);
        AstNode *else_b = NULL;
        if (parser_match(p, TOKEN_OTHERWISE)) else_b = parse_block(p);
        parser_expect(p, TOKEN_END, "expected 'end'");
        return node_if(cond, then_b, else_b, line);
    }

    if (parser_check(p, TOKEN_COUNT)) {
        int line = p->current.line;
        parser_advance(p);
        char *var = parser_expect(p, TOKEN_IDENTIFIER, "expected loop variable");
        parser_expect(p, TOKEN_FROM, "expected 'from'");
        AstNode *from = parse_expression(p);
        parser_expect(p, TOKEN_TO, "expected 'to'");
        AstNode *to = parse_expression(p);
        parser_expect(p, TOKEN_THEN, "expected 'then'");
        AstNode *body = parse_block(p);
        parser_expect(p, TOKEN_END, "expected 'end'");
        AstNode *n = node_count(var, from, to, body, line);
        free(var);
        return n;
    }

    if (parser_check(p, TOKEN_LOOP)) {
        int line = p->current.line;
        parser_advance(p);
        char *var = parser_expect(p, TOKEN_IDENTIFIER, "expected loop variable");
        parser_expect(p, TOKEN_THROUGH, "expected 'through'");
        AstNode *iterable = parse_expression(p);
        parser_expect(p, TOKEN_THEN, "expected 'then'");
        AstNode *body = parse_block(p);
        parser_expect(p, TOKEN_END, "expected 'end'");
        AstNode *n = node_loop_through(var, iterable, body, line);
        free(var);
        return n;
    }

    if (parser_check(p, TOKEN_WHILE)) {
        int line = p->current.line;
        parser_advance(p);
        AstNode *cond = parse_condition(p);
        parser_expect(p, TOKEN_THEN, "expected 'then' after while condition");
        AstNode *body = parse_block(p);
        parser_expect(p, TOKEN_END, "expected 'end' after while");
        return node_while(cond, body, line);
    }

    if (parser_check(p, TOKEN_BREAK)) {
        int line = p->current.line;
        parser_advance(p);
        return node_break(line);
    }

    if (parser_check(p, TOKEN_CONTINUE)) {
        int line = p->current.line;
        parser_advance(p);
        return node_continue(line);
    }

    if (parser_check(p, TOKEN_MATCH)) {
        int line = p->current.line;
        parser_advance(p);
        AstNode *val;
        if (parser_check(p, TOKEN_IDENTIFIER)) {
            int vline = p->current.line;
            char *name = copy_token(p);
            parser_advance(p);
            // handle possessive/index chains but not 'with' calls
            AstNode *base = node_identifier(name, vline);
            free(name);
            // handle postfix like a's b or a[0] for match value
            while (parser_check(p, TOKEN_PSS) || parser_check(p, TOKEN_LBRACKET)) {
                if (parser_check(p, TOKEN_PSS)) {
                    int pline = p->current.line;
                    parser_advance(p);
                    char *field = parser_expect(p, TOKEN_IDENTIFIER, "expected name after 's");
                    base = node_possessive(base, field, pline);
                    free(field);
                } else if (parser_check(p, TOKEN_LBRACKET)) {
                    int pline = p->current.line;
                    parser_advance(p);
                    AstNode *idx = parse_expression(p);
                    parser_expect(p, TOKEN_RBRACKET, "expected ']'");
                    base = node_index(base, idx, pline);
                }
            }
            val = base;
        } else {
            val = parse_expression(p);
        }
        AstNode *m = node_match(val, line);
        while (parser_match(p, TOKEN_WITH)) {
            AstNode *pat;
            if (parser_check(p, TOKEN_IDENTIFIER) && p->current.length==1 && p->current.start[0]=='_') {
                int pline = p->current.line;
                parser_advance(p);
                pat = node_identifier("_", pline);
            } else {
                pat = parse_expression(p);
            }
            parser_expect(p, TOKEN_THEN, "expected 'then' after match pattern");
            NodeList stmts = nodelist_create();
            while (!parser_check(p, TOKEN_WITH) && !parser_check(p, TOKEN_END) && !parser_check(p, TOKEN_EOF) && !p->had_error) {
                AstNode *stmt = parse_statement(p);
                if (stmt) nodelist_push(&stmts, stmt);
                parser_match(p, TOKEN_DOT);
            }
            AstNode *body = node_block(stmts);
            node_match_add_case(m, pat, body);
        }
        parser_expect(p, TOKEN_END, "expected 'end' after match");
        return m;
    }

    if (parser_check(p, TOKEN_TRY)) {
        int line = p->current.line;
        parser_advance(p);
        AstNode *body = parse_block(p);
        parser_expect(p, TOKEN_CATCH, "expected 'catch'");
        char *evar = parser_expect(p, TOKEN_IDENTIFIER, "expected error variable after 'catch'");
        AstNode *catch_b = parse_block(p);
        parser_expect(p, TOKEN_END, "expected 'end'");
        AstNode *n = node_try(body, evar, catch_b, line);
        free(evar);
        return n;
    }

    if (parser_check(p, TOKEN_RAISE)) {
        int line = p->current.line;
        parser_advance(p);
        return node_raise(parse_expression(p), line);
    }

    if (parser_check(p, TOKEN_ASSERT)) {
        int line = p->current.line;
        parser_advance(p);
        AstNode *value = parse_expression(p);
        AstNode *expected = NULL;
        if (parser_match(p, TOKEN_IS)) {
            expected = parse_expression(p);
        }
        return node_assert(value, expected, line);
    }

    if (parser_check(p, TOKEN_START) || parser_check(p, TOKEN_ASYNC)) {
        int line = p->current.line;
        parser_advance(p);
        AstNode *call = parse_expression(p);
        parser_match(p, TOKEN_DOT);
        parser_expect(p, TOKEN_AS, "expected 'as' after start");
        parser_expect(p, TOKEN_TASK, "expected 'task' after 'as'");
        char *name = parser_expect(p, TOKEN_IDENTIFIER, "expected task name");
        AstNode *n = node_start_task(call, name, line);
        free(name);
        return n;
    }

    return parse_expression(p);
}

void parser_init(Parser *parser, const char *source) {
    lexer_init(&parser->lexer, source);
    parser_advance(parser);
    parser->had_error = 0;
    parser->error_msg[0] = '\0';
}

AstNode *parser_parse(Parser *parser) {
    NodeList stmts = nodelist_create();
    while (!parser_check(parser, TOKEN_EOF) && !parser->had_error) {
        AstNode *stmt = parse_statement(parser);
        if (stmt) nodelist_push(&stmts, stmt);
        parser_match(parser, TOKEN_DOT);
    }
    return node_program(stmts);
}
