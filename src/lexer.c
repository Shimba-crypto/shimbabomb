#define _POSIX_C_SOURCE 200809L
#include "lexer.h"
#include <string.h>
#include <ctype.h>

static char peek(Lexer *l) { return l->source[l->current]; }
static char peek_next(Lexer *l) {
    if (l->source[l->current] == '\0') return '\0';
    return l->source[l->current + 1];
}
static char advance(Lexer *l) { return l->source[l->current++]; }
static int is_alpha(char c) { return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='_'; }
static int is_alnum(char c) { return is_alpha(c)||isdigit(c); }

static void skip_whitespace(Lexer *l) {
    for (;;) {
        char c = peek(l);
        if (c==' '||c=='\t'||c=='\r') advance(l);
        else if (c=='\n') { l->line++; advance(l); }
        else if (c=='#') { while (peek(l)!='\n' && peek(l)!='\0') advance(l); }
        else break;
    }
}

static Token make_token(Lexer *l, TokenType type) {
    Token t = {type, &l->source[l->start], l->current - l->start, l->line};
    return t;
}
static Token error_token(Lexer *l, const char *msg) {
    Token t = {TOKEN_ERROR, msg, (int)strlen(msg), l->line};
    return t;
}

static TokenType check_keyword(Lexer *l, int len, const char *rest, TokenType type) {
    if (l->current - l->start == len && memcmp(l->source + l->start, rest, len) == 0)
        return type;
    return TOKEN_IDENTIFIER;
}

static TokenType resolve_keyword(Lexer *l) {
    const char *s = l->source + l->start;
    int len = l->current - l->start;
    switch (s[0]) {
        case 'a':
            if (len==6 && !memcmp(s,"assert",6)) return TOKEN_ASSERT;
            if (len==5 && !memcmp(s,"async",5)) return TOKEN_ASYNC;
            if (len==2 && !memcmp(s,"as",2)) return TOKEN_AS;
            return check_keyword(l, 3, "and", TOKEN_AND);
        case 'b':
            if (len==5 && !memcmp(s,"break",5)) return TOKEN_BREAK;
            if (len==5 && !memcmp(s,"bring",5)) return TOKEN_BRING;
            return check_keyword(l, 4, "back", TOKEN_BACK);
        case 'c':
            if (len==8 && !memcmp(s,"continue",8)) return TOKEN_CONTINUE;
            if (len==5 && !memcmp(s,"class",5)) return TOKEN_CLASS;
            if (len==5 && !memcmp(s,"catch",5)) return TOKEN_CATCH;
            return check_keyword(l, 5, "count", TOKEN_COUNT);
        case 'd': return check_keyword(l, 6, "define", TOKEN_DEFINE);
        case 'e': return check_keyword(l, 3, "end", TOKEN_END);
        case 'f':
            if (len==2 && !memcmp(s,"fn",2)) return TOKEN_FN;
            if (len==4 && !memcmp(s,"from",4)) return TOKEN_FROM;
            return check_keyword(l, 5, "false", TOKEN_FALSE);
        case 'g': return check_keyword(l, 4, "give", TOKEN_GIVE);
        case 'i':
            if (len==2 && s[1]=='f') return TOKEN_IF;
            return check_keyword(l, 2, "is", TOKEN_IS);
        case 'l':
            if (len==4 && !memcmp(s,"less",4)) return TOKEN_LESS;
            if (len==6 && !memcmp(s,"lambda",6)) return TOKEN_LAMBDA;
            return check_keyword(l, 4, "loop", TOKEN_LOOP);
        case 'm':
            if (len==3 && !memcmp(s,"mod",3)) return TOKEN_MOD;
            if (len==5 && !memcmp(s,"match",5)) return TOKEN_MATCH;
            if (len==5 && !memcmp(s,"minus",5)) return TOKEN_MINUS;
            return check_keyword(l, 6, "method", TOKEN_METHOD);
        case 'n':
            if (len==3 && !memcmp(s,"new",3)) return TOKEN_NEW;
            return check_keyword(l, 3, "not", TOKEN_NOT);
        case 'o':
            if (len==2 && !memcmp(s,"or",2)) return TOKEN_OR;
            return check_keyword(l, 9, "otherwise", TOKEN_OTHERWISE);
        case 'p':
            if (len==3 && !memcmp(s,"pls",3)) return TOKEN_PLS;
            return check_keyword(l, 4, "plus", TOKEN_PLUS);
        case 'r': return check_keyword(l, 5, "raise", TOKEN_RAISE);
        case 's':
            if (len==5 && !memcmp(s,"start",5)) return TOKEN_START;
            if (len==3 && !memcmp(s,"say",3)) return TOKEN_SAY;
            if (len==4 && !memcmp(s,"self",4)) return TOKEN_SELF;
            return check_keyword(l, 3, "set", TOKEN_SET);
        case 't':
            if (len==5 && !memcmp(s,"times",5)) return TOKEN_STAR;
            if (len==4 && !memcmp(s,"task",4)) return TOKEN_TASK;
            if (len==4 && !memcmp(s,"then",4)) return TOKEN_THEN;
            if (len==7 && !memcmp(s,"through",7)) return TOKEN_THROUGH;
            if (len==3 && !memcmp(s,"try",3)) return TOKEN_TRY;
            if (len==4 && !memcmp(s,"true",4)) return TOKEN_TRUE;
            return check_keyword(l, 2, "to", TOKEN_TO);
        case 'w':
            if (len==5 && !memcmp(s,"await",5)) return TOKEN_AWAIT;
            if (len==4 && !memcmp(s,"wait",4)) return TOKEN_WAIT;
            if (len==5 && !memcmp(s,"while",5)) return TOKEN_WHILE;
            return check_keyword(l, 4, "with", TOKEN_WITH);
    }
    return TOKEN_IDENTIFIER;
}

void lexer_init(Lexer *lexer, const char *source) {
    lexer->source = source;
    lexer->start = 0;
    lexer->current = 0;
    lexer->line = 1;

    skip_whitespace(lexer);

    // "SB" signature stamp on the first line — recognized and skipped
    if (peek(lexer) == 'S' && peek_next(lexer) == 'B') {
        char after = lexer->source[lexer->current + 2];
        if (!is_alpha(after) && !isdigit(after)) {
            advance(lexer);
            advance(lexer);
            skip_whitespace(lexer);
        }
    }

    lexer->start = lexer->current;
}

Token lexer_next_token(Lexer *lexer) {
    skip_whitespace(lexer);
    lexer->start = lexer->current;
    if (peek(lexer) == '\0') return make_token(lexer, TOKEN_EOF);

    char c = advance(lexer);

    if (c == '"') {
        while (peek(lexer) != '"' && peek(lexer) != '\0') {
            if (peek(lexer) == '\\' && peek_next(lexer) != '\0') {
                advance(lexer); advance(lexer);
            } else {
                if (peek(lexer) == '\n') lexer->line++;
                advance(lexer);
            }
        }
        if (peek(lexer) == '\0') return error_token(lexer, "unterminated string");
        advance(lexer);
        return make_token(lexer, TOKEN_STRING);
    }

    if (isdigit(c)) {
        while (isdigit(peek(lexer))) advance(lexer);
        if (peek(lexer) == '.' && isdigit(peek_next(lexer))) {
            advance(lexer);
            while (isdigit(peek(lexer))) advance(lexer);
        }
        return make_token(lexer, TOKEN_NUMBER);
    }

    if (is_alpha(c)) {
        while (is_alnum(peek(lexer))) advance(lexer);

        int klen = lexer->current - lexer->start;
        const char *ks = lexer->source + lexer->start;

        // "is less than" / "is greater than" / "is equal to"
        if (klen == 2 && !memcmp(ks, "is", 2)) {
            int tmp = lexer->current;
            while (peek(lexer) == ' ') advance(lexer);
            if (is_alpha(peek(lexer))) {
                int nws = lexer->current;
                while (is_alnum(peek(lexer))) advance(lexer);
                if (lexer->current-nws==3 && !memcmp(lexer->source+nws, "not", 3))
                    return make_token(lexer, TOKEN_NOTEQ);
                lexer->current = tmp;
                while (peek(lexer) == ' ') advance(lexer);
            }
            if (is_alpha(peek(lexer))) {
                int ws = lexer->current;
                while (is_alnum(peek(lexer))) advance(lexer);
                int wl = lexer->current - ws;
                if (wl==4 && !memcmp(lexer->source+ws, "less", 4)) {
                    while (peek(lexer) == ' ') advance(lexer);
                    if (is_alpha(peek(lexer))) {
                        int w2 = lexer->current;
                        while (is_alnum(peek(lexer))) advance(lexer);
                        if (lexer->current-w2==4 && !memcmp(lexer->source+w2, "than", 4))
                            return make_token(lexer, TOKEN_LESS);
                    }
                    lexer->current = tmp;
                } else if (wl==7 && !memcmp(lexer->source+ws, "greater", 7)) {
                    while (peek(lexer) == ' ') advance(lexer);
                    if (is_alpha(peek(lexer))) {
                        int w2 = lexer->current;
                        while (is_alnum(peek(lexer))) advance(lexer);
                        if (lexer->current-w2==4 && !memcmp(lexer->source+w2, "than", 4))
                            return make_token(lexer, TOKEN_GREATER);
                    }
                    lexer->current = tmp;
                } else if (wl==5 && !memcmp(lexer->source+ws, "equal", 5)) {
                    while (peek(lexer) == ' ') advance(lexer);
                    if (is_alpha(peek(lexer))) {
                        int w2 = lexer->current;
                        while (is_alnum(peek(lexer))) advance(lexer);
                        if (lexer->current-w2==2 && !memcmp(lexer->source+w2, "to", 2))
                            return make_token(lexer, TOKEN_EQUAL);
                    }
                    lexer->current = tmp;
                } else {
                    lexer->current = tmp;
                }
            } else {
                lexer->current = tmp;
            }
        }

        // "divided by" / "divided evenly by"
        if (klen == 7 && !memcmp(ks, "divided", 7)) {
            int tmp = lexer->current;
            while (peek(lexer) == ' ') advance(lexer);
            if (is_alpha(peek(lexer))) {
                int w2 = lexer->current;
                while (is_alnum(peek(lexer))) advance(lexer);
                if (lexer->current-w2 == 6 && !memcmp(lexer->source+w2, "evenly", 6)) {
                    while (peek(lexer) == ' ') advance(lexer);
                    if (is_alpha(peek(lexer))) {
                        int w3 = lexer->current;
                        while (is_alnum(peek(lexer))) advance(lexer);
                        if (lexer->current-w3 == 2 && !memcmp(lexer->source+w3, "by", 2))
                            return make_token(lexer, TOKEN_INTDIV);
                    }
                }
                if (lexer->current-w2 == 2 && !memcmp(lexer->source+w2, "by", 2))
                    return make_token(lexer, TOKEN_SLASH);
            }
            lexer->current = tmp;
        }

        return make_token(lexer, resolve_keyword(lexer));
    }

    if (c=='+') return make_token(lexer, TOKEN_PLUS);
    if (c=='-') return make_token(lexer, TOKEN_MINUS);
    if (c=='*') return make_token(lexer, TOKEN_STAR);
    if (c=='/') return make_token(lexer, TOKEN_SLASH);
    if (c=='.') return make_token(lexer, TOKEN_DOT);
    if (c=='(') return make_token(lexer, TOKEN_LPAREN);
    if (c==')') return make_token(lexer, TOKEN_RPAREN);
    if (c==',') return make_token(lexer, TOKEN_COMMA);
    if (c=='[') return make_token(lexer, TOKEN_LBRACKET);
    if (c==']') return make_token(lexer, TOKEN_RBRACKET);
    if (c=='\'' && peek(lexer)=='s') { advance(lexer); return make_token(lexer, TOKEN_PSS); }

    return error_token(lexer, "unexpected character");
}

const char *token_type_name(TokenType type) {
    switch (type) {
        case TOKEN_NUMBER: return "NUMBER";   case TOKEN_STRING: return "STRING";
        case TOKEN_IDENTIFIER: return "IDENT"; case TOKEN_SET: return "SET";
        case TOKEN_TO: return "TO";           case TOKEN_DEFINE: return "DEFINE";
        case TOKEN_WITH: return "WITH";       case TOKEN_AND: return "AND";
        case TOKEN_AS: return "AS";           case TOKEN_IF: return "IF";
        case TOKEN_THEN: return "THEN";       case TOKEN_OTHERWISE: return "OTHERWISE";
        case TOKEN_END: return "END";         case TOKEN_COUNT: return "COUNT";
        case TOKEN_FROM: return "FROM";       case TOKEN_SAY: return "SAY";
        case TOKEN_GIVE: return "GIVE";       case TOKEN_BACK: return "BACK";
        case TOKEN_TRUE: return "TRUE";       case TOKEN_FALSE: return "FALSE";
        case TOKEN_NOT: return "NOT";         case TOKEN_IS: return "IS";
        case TOKEN_LESS: return "LESS";       case TOKEN_GREATER: return "GREATER";
        case TOKEN_EQUAL: return "EQUAL";     case TOKEN_PLUS: return "PLUS";
        case TOKEN_MINUS: return "MINUS";     case TOKEN_STAR: return "STAR";
        case TOKEN_SLASH: return "SLASH";     case TOKEN_DOT: return "DOT";
        case TOKEN_LPAREN: return "LPAREN";   case TOKEN_RPAREN: return "RPAREN";
        case TOKEN_COMMA: return "COMMA";     case TOKEN_PSS: return "PSS";
        case TOKEN_LBRACKET: return "LBRACKET"; case TOKEN_RBRACKET: return "RBRACKET";
        case TOKEN_LOOP: return "LOOP";       case TOKEN_THROUGH: return "THROUGH";
        case TOKEN_TRY: return "TRY";         case TOKEN_CATCH: return "CATCH";
        case TOKEN_RAISE: return "RAISE";     case TOKEN_CLASS: return "CLASS";
        case TOKEN_NEW: return "NEW";         case TOKEN_METHOD: return "METHOD";
        case TOKEN_SELF: return "SELF";       case TOKEN_WHILE: return "WHILE";
        case TOKEN_BREAK: return "BREAK";     case TOKEN_CONTINUE: return "CONTINUE";
        case TOKEN_MATCH: return "MATCH";     case TOKEN_ASSERT: return "ASSERT";
        case TOKEN_FN: return "FN";         case TOKEN_LAMBDA: return "LAMBDA";
        case TOKEN_NOTEQ: return "NOTEQ"; case TOKEN_MOD: return "MOD";
        case TOKEN_INTDIV: return "INTDIV"; case TOKEN_OR: return "OR";
        case TOKEN_PLS: return "PLS";         case TOKEN_BRING: return "BRING";
        case TOKEN_START: return "START";     case TOKEN_TASK: return "TASK";
        case TOKEN_WAIT: return "WAIT";       case TOKEN_ASYNC: return "ASYNC";
        case TOKEN_AWAIT: return "AWAIT";
        case TOKEN_EOF: return "EOF";         case TOKEN_ERROR: return "ERROR";
    }
    return "?";
}
