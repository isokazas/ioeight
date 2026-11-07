#include "lexer.h"

#include <ctype.h>
#include <string.h>

const char *token_name(TokenType t) {
    static const char *names[] = {
#define X(name) #name,
        TOKEN_LIST(X)
#undef X
    };
    return names[t];
}

void lexer_init(Lexer *lx, const char *src) {
    lx->src = lx->cur = src;
    lx->line = 1;
    lx->col = 1;
    lx->depth = 0;
    lx->last_was_nl = 1;
}

static char peek(Lexer *lx, int n) {
    return lx->cur[n];
}

static char advance(Lexer *lx) {
    char c = *lx->cur++;
    if (c == '\n') {
        lx->line++;
        lx->col = 1;
    } else {
        lx->col++;
    }
    return c;
}

static Token make(Lexer *lx, TokenType t, const char *start, int line, int col) {
    Token tok;
    tok.type = t;
    tok.start = start;
    tok.length = (size_t)(lx->cur - start);
    tok.line = line;
    tok.col = col;
    tok.msg = NULL;
    return tok;
}

static Token error(Lexer *lx, const char *msg, const char *start, int line, int col) {
    Token tok = make(lx, TOK_ERROR, start, line, col);
    tok.msg = msg;
    return tok;
}

typedef struct {
    const char *word;
    TokenType type;
} Keyword;

static const Keyword keywords[] = {
    {"if", TOK_IF},
    {"elif", TOK_ELIF},
    {"else", TOK_ELSE},
    {"while", TOK_WHILE},
    {"for", TOK_FOR},
    {"def", TOK_DEF},
    {"return", TOK_RETURN},
    {"break", TOK_BREAK},
    {"continue", TOK_CONTINUE},
    {"and", TOK_AND},
    {"or", TOK_OR},
    {"not", TOK_NOT},
    {"true", TOK_TRUE},
    {"false", TOK_FALSE},
    {"int", TOK_T_INT},
    {"flt", TOK_T_FLT},
    {"str", TOK_T_STR},
    {"bool", TOK_T_BOOL},
    {"pnt", TOK_T_PNT},
    {"imgint", TOK_T_IMGINT},
    {"imgflt", TOK_T_IMGFLT},
    {"input", TOK_INPUT},
    {"addon", TOK_ADDON},
    {"from", TOK_FROM},
    {"addevery", TOK_ADDEVERY},
    {"as", TOK_AS},
};

static TokenType ident_type(const char *s, size_t len) {
    for (size_t i = 0; i < sizeof(keywords) / sizeof(keywords[0]); i++) {
        if (strlen(keywords[i].word) == len && memcmp(keywords[i].word, s, len) == 0) {
            return keywords[i].type;
        }
    }
    return TOK_IDENT;
}

static int skip_trivia(Lexer *lx, Token *err) {
    for (;;) {
        char c = peek(lx, 0);

        if (c == ' ' || c == '\t' || c == '\r') {
            advance(lx);
            continue;
        }

        if (c == '!' && peek(lx, 1) == '!' && peek(lx, 2) == '!') {
            while (peek(lx, 0) && peek(lx, 0) != '\n') {
                advance(lx);
            }
            continue;
        }

        if (c == '/' && peek(lx, 1) == '!' && peek(lx, 2) == '!') {
            const char *start = lx->cur;
            int line = lx->line;
            int col = lx->col;
            advance(lx);
            advance(lx);
            advance(lx);

            for (;;) {
                if (!peek(lx, 0)) {
                    *err = error(lx, "unterminated block comment (missing !!/)", start, line, col);
                    return 0;
                }
                if (peek(lx, 0) == '!' && peek(lx, 1) == '!' && peek(lx, 2) == '/') {
                    advance(lx);
                    advance(lx);
                    advance(lx);
                    break;
                }
                advance(lx);
            }
            continue;
        }

        return 1;
    }
}

static Token lex_number(Lexer *lx, const char *start, int line, int col) {
    while (isdigit((unsigned char)peek(lx, 0))) {
        advance(lx);
    }

    if (peek(lx, 0) == '.' && isdigit((unsigned char)peek(lx, 1))) {
        advance(lx);
        while (isdigit((unsigned char)peek(lx, 0))) {
            advance(lx);
        }
        return make(lx, TOK_FLT_LIT, start, line, col);
    }

    return make(lx, TOK_INT_LIT, start, line, col);
}

static Token lex_string(Lexer *lx, const char *start, int line, int col) {
    advance(lx);
    for (;;) {
        char c = peek(lx, 0);
        if (c == '\0' || c == '\n') {
            return error(lx, "unterminated string", start, line, col);
        }
        advance(lx);

        if (c == '"') {
            return make(lx, TOK_STR_LIT, start, line, col);
        }

        if (c == '\\') {
            char e = peek(lx, 0);
            if (e == 'n' || e == 't' || e == '"' || e == '\\') {
                advance(lx);
            } else {
                return error(lx, "unknown escape sequence", start, line, col);
            }
        }
    }
}

Token lexer_next(Lexer *lx) {
    Token err;

    for (;;) {
        if (!skip_trivia(lx, &err)) {
            return err;
        }

        if (peek(lx, 0) == '\n') {
            const char *start = lx->cur;
            int line = lx->line;
            int col = lx->col;
            advance(lx);

            if (lx->depth > 0 || lx->last_was_nl) {
                continue;
            }

            lx->last_was_nl = 1;
            return make(lx, TOK_NL, start, line, col);
        }

        break;
    }

    const char *start = lx->cur;
    int line = lx->line;
    int col = lx->col;
    char c = peek(lx, 0);

    if (c == '\0') {
        return make(lx, TOK_EOF, start, line, col);
    }

    lx->last_was_nl = 0;
    advance(lx);

    if (isalpha((unsigned char)c) || c == '_') {
        while (isalnum((unsigned char)peek(lx, 0)) || peek(lx, 0) == '_') {
            advance(lx);
        }
        return make(lx, ident_type(start, (size_t)(lx->cur - start)), start, line, col);
    }

    if (isdigit((unsigned char)c)) {
        return lex_number(lx, start, line, col);
    }

    if (c == '"') {
        return lex_string(lx, start, line, col);
    }

#define MATCH(ch) (peek(lx, 0) == (ch) ? (advance(lx), 1) : 0)
#define TOK(t) return make(lx, t, start, line, col)

    switch (c) {
        case '(':
            lx->depth++;
            TOK(TOK_LPAREN);
        case ')':
            if (lx->depth > 0) lx->depth--;
            TOK(TOK_RPAREN);
        case '[':
            lx->depth++;
            TOK(TOK_LBRACKET);
        case ']':
            if (lx->depth > 0) lx->depth--;
            TOK(TOK_RBRACKET);
        case '{':
            TOK(TOK_LBRACE);
        case '}':
            TOK(TOK_RBRACE);
        case ',':
            TOK(TOK_COMMA);
        case ':':
            TOK(TOK_COLON);
        case '.':
            TOK(TOK_DOT);
        case '$':
            TOK(TOK_DOLLAR);
        case ';':
            TOK(TOK_SEMI);
        case '+':
            TOK(MATCH('=') ? TOK_PLUS_EQ : TOK_PLUS);
        case '-':
            if (MATCH('>')) TOK(TOK_ARROW);
            TOK(MATCH('=') ? TOK_MINUS_EQ : TOK_MINUS);
        case '*':
            TOK(MATCH('=') ? TOK_STAR_EQ : TOK_STAR);
        case '^':
            TOK(MATCH('=') ? TOK_CARET_EQ : TOK_CARET);
        case '~':
            TOK(TOK_TILDA);
        case '/':
            if (MATCH('/')) TOK(MATCH('=') ? TOK_SLASH2_EQ : TOK_SLASH2);
            TOK(MATCH('=') ? TOK_SLASH_EQ : TOK_SLASH);
        case '%':
            if (MATCH('%')) TOK(MATCH('=') ? TOK_PCT2_EQ : TOK_PCT2);
            return error(lx, "modulo is %%, not %", start, line, col);
        case '=':
            TOK(MATCH('=') ? TOK_EQ : TOK_ASSIGN);
        case '<':
            TOK(MATCH('=') ? TOK_LE : TOK_LT);
        case '>':
            if (MATCH('>')) TOK(TOK_PTR);
            TOK(MATCH('=') ? TOK_GE : TOK_GT);
        case '!':
            if (MATCH('=')) TOK(TOK_NEQ);
            return error(lx, "unexpected '!' (use 'not', or '!=')", start, line, col);
        default:
            break;
    }

#undef MATCH
#undef TOK
    return error(lx, "unexpected character", start, line, col);
}
