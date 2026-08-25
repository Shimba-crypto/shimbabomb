#ifndef SB_PARSER_H
#define SB_PARSER_H

#include "ast.h"
#include "lexer.h"

typedef struct {
    Lexer lexer;
    Token current;
    Token previous;
    int had_error;
    char error_msg[256];
} Parser;

void parser_init(Parser *parser, const char *source);
AstNode *parser_parse(Parser *parser);

#endif
