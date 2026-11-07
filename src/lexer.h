#ifndef IO108_LEXER_H
#define IO108_LEXER_H

#include <stddef.h>

#define TOKEN_LIST(X) \
X(EOF) X(NL) X(ERROR) \
X(IDENT) X(INT_LIT) X(FLT_LIT) X(STR_LIT) \
X(IF) X(ELIF) X(ELSE) X(WHILE) X(FOR) X(DEF) X(RETURN) X(BREAK) X(CONTINUE) \
X(AND) X(OR) X(NOT) X(TRUE) X(FALSE) \
X(T_INT) X(T_FLT) X(T_STR) X(T_BOOL) X(T_PNT) X(T_IMGINT) X(T_IMGFLT) X(INPUT) \
X(ADDON) X(FROM) X(ADDEVERY) X(AS) \
X(LPAREN) X(RPAREN) X(LBRACKET) X(RBRACKET) X(LBRACE) X(RBRACE) \
X(COMMA) X(COLON) X(DOT) X(ARROW) X(DOLLAR) X(SEMI) \
X(PLUS) X(MINUS) X(STAR) X(SLASH) X(SLASH2) X(PCT2) X(CARET) X(TILDA) X(PTR) \
X(ASSIGN) X(PLUS_EQ) X(MINUS_EQ) X(STAR_EQ) X(SLASH_EQ) X(SLASH2_EQ) \
X(PCT2_EQ) X(CARET_EQ) \
X(EQ) X(NEQ) X(LT) X(LE) X(GT) X(GE)

typedef enum {
    #define X(name) TOK_##name,
    TOKEN_LIST(X)
    #undef X
} TokenType;

typedef struct {
    TokenType type;
    const char *start;
    size_t length;
    int line;
    int col;
    const char *msg;
} Token;

typedef struct {
    const char *src;
    const char *cur;
    int line, col;
    int depth;
    int last_was_nl;
} Lexer;

void lexer_init(Lexer *lx, const char *src);
Token lexer_next(Lexer *lx);
const char *token_name(TokenType t);

#endif
