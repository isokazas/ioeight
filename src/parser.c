#include "parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

typedef struct {
    Token *tokens;
    int count;
    int pos;
} Parser;

static Parser parser;

static Token peek(void) {
    if (parser.pos >= parser.count) {
        return (Token){TOK_EOF, "", 0, 0, 0, NULL};
    }
    return parser.tokens[parser.pos];
}

static Token advance(void) {
    Token t = peek();
    parser.pos++;
    return t;
}

static int check(TokenType type) {
    return peek().type == type;
}

static int match(TokenType type) {
    if (check(type)) {
        advance();
        return 1;
    }
    return 0;
}

static int match_any(int count, ...) {
    va_list ap;
    va_start(ap, count);
    for (int i = 0; i < count; i++) {
        if (check(va_arg(ap, TokenType))) {
            va_end(ap);
            advance();
            return 1;
        }
    }
    va_end(ap);
    return 0;
}

static Token consume(TokenType type, const char *msg) {
    if (!check(type)) {
        fprintf(stderr, "Parse error at line %d: %s (expected %s, got %s)\n",
                peek().line, msg, token_name(type), token_name(peek().type));
    }
    return advance();
}

static char *copy_token(Token t) {
    char *s = malloc(t.length + 1);
    memcpy(s, t.start, t.length);
    s[t.length] = '\0';
    return s;
}

static Ast *new_ast(AstKind kind) {
    Ast *a = calloc(1, sizeof(Ast));
    a->kind = kind;
    return a;
}

static void skip_newlines(void) {
    while (match(TOK_NL)) {
        /* skip */
    }
}

static Ast *parse_expr(void);
static Ast *parse_stmt(void);
static Ast *parse_block(void);

static Ast *parse_primary(void) {
    skip_newlines();

    if (check(TOK_INT_LIT)) {
        Token t = advance();
        Ast *n = new_ast(AST_INT);
        n->int_value = atoi(t.start);
        return n;
    }

    if (check(TOK_FLT_LIT)) {
        Token t = advance();
        Ast *n = new_ast(AST_FLT);
        n->float_value = atof(t.start);
        return n;
    }

    if (check(TOK_STR_LIT)) {
        Token t = advance();
        Ast *n = new_ast(AST_STR);
        n->str_value = copy_token(t);
        return n;
    }

    if (check(TOK_TRUE)) {
        advance();
        Ast *n = new_ast(AST_BOOL);
        n->bool_value = 1;
        return n;
    }

    if (check(TOK_FALSE)) {
        advance();
        Ast *n = new_ast(AST_BOOL);
        n->bool_value = 0;
        return n;
    }

    if (check(TOK_IDENT)) {
        Token t = advance();
        Ast *n = new_ast(AST_IDENT);
        n->name = copy_token(t);
        return n;
    }

    if (match(TOK_LPAREN)) {
        Ast *e = parse_expr();
        consume(TOK_RPAREN, "expected ')'");
        return e;
    }

    if (match(TOK_LBRACKET)) {
        Ast *list = new_ast(AST_LIST);
        list->args = calloc(256, sizeof(Ast *));
        list->arg_count = 0;

        if (!check(TOK_RBRACKET)) {
            do {
                list->args[list->arg_count++] = parse_expr();
            } while (match(TOK_COMMA));
        }

        consume(TOK_RBRACKET, "expected ']'");
        return list;
    }

    fprintf(stderr, "Parse error at line %d: unexpected token %s\n",
            peek().line, token_name(peek().type));
    return NULL;
}

static Ast *parse_postfix(void) {
    Ast *expr = parse_primary();
    if (!expr) return NULL;

    for (;;) {
        if (match(TOK_DOT)) {
            Token member = consume(TOK_IDENT, "expected member name");
            Ast *mem = new_ast(AST_MEMBER_CALL);
            mem->left = expr;
            mem->member = copy_token(member);

            if (match(TOK_LPAREN)) {
                mem->args = calloc(16, sizeof(Ast *));
                mem->arg_count = 0;
                if (!check(TOK_RPAREN)) {
                    do {
                        mem->args[mem->arg_count++] = parse_expr();
                    } while (match(TOK_COMMA));
                }
                consume(TOK_RPAREN, "expected ')'");
            }

            expr = mem;
        } else if (match(TOK_LPAREN)) {
            Ast *call = new_ast(AST_CALL);
            call->left = expr;
            call->args = calloc(16, sizeof(Ast *));
            call->arg_count = 0;

            if (!check(TOK_RPAREN)) {
                do {
                    call->args[call->arg_count++] = parse_expr();
                } while (match(TOK_COMMA));
            }

            consume(TOK_RPAREN, "expected ')'");
            expr = call;
        } else if (match(TOK_LBRACKET)) {
            Ast *idx = new_ast(AST_BINARY_OP);
            idx->op = TOK_LBRACKET;
            idx->left = expr;
            idx->right = parse_expr();
            consume(TOK_RBRACKET, "expected ']'");
            expr = idx;
        } else {
            break;
        }
    }

    return expr;
}

static Ast *parse_unary(void) {
    if (match(TOK_MINUS)) {
        Ast *u = new_ast(AST_UNARY_OP);
        u->op = TOK_MINUS;
        u->child = parse_unary();
        return u;
    }
    if (match(TOK_NOT)) {
        Ast *u = new_ast(AST_UNARY_OP);
        u->op = TOK_NOT;
        u->child = parse_unary();
        return u;
    }
    return parse_postfix();
}

static Ast *parse_power(void) {
    Ast *left = parse_unary();
    if (match(TOK_CARET)) {
        Ast *right = parse_unary();
        Ast *op = new_ast(AST_BINARY_OP);
        op->op = TOK_CARET;
        op->left = left;
        op->right = right;
        return op;
    }
    return left;
}

static Ast *parse_term(void) {
    Ast *left = parse_power();

    while (check(TOK_STAR) || check(TOK_SLASH) || check(TOK_SLASH2) || check(TOK_PCT2)) {
        TokenType op = advance().type;
        Ast *right = parse_power();
        Ast *node = new_ast(AST_BINARY_OP);
        node->op = op;
        node->left = left;
        node->right = right;
        left = node;
    }

    return left;
}

static Ast *parse_additive(void) {
    Ast *left = parse_term();

    while (check(TOK_PLUS) || check(TOK_MINUS)) {
        TokenType op = advance().type;
        Ast *right = parse_term();
        Ast *node = new_ast(AST_BINARY_OP);
        node->op = op;
        node->left = left;
        node->right = right;
        left = node;
    }

    return left;
}

static Ast *parse_relational(void) {
    Ast *left = parse_additive();

    if (check(TOK_LT) || check(TOK_LE) || check(TOK_GT) || check(TOK_GE)) {
        TokenType op = advance().type;
        Ast *right = parse_additive();
        Ast *node = new_ast(AST_BINARY_OP);
        node->op = op;
        node->left = left;
        node->right = right;
        return node;
    }

    return left;
}

static Ast *parse_equality(void) {
    Ast *left = parse_relational();

    while (check(TOK_EQ) || check(TOK_NEQ)) {
        TokenType op = advance().type;
        Ast *right = parse_relational();
        Ast *node = new_ast(AST_BINARY_OP);
        node->op = op;
        node->left = left;
        node->right = right;
        left = node;
    }

    return left;
}

static Ast *parse_and(void) {
    Ast *left = parse_equality();

    while (match(TOK_AND)) {
        Ast *right = parse_equality();
        Ast *node = new_ast(AST_BINARY_OP);
        node->op = TOK_AND;
        node->left = left;
        node->right = right;
        left = node;
    }

    return left;
}

static Ast *parse_or(void) {
    Ast *left = parse_and();

    while (match(TOK_OR)) {
        Ast *right = parse_and();
        Ast *node = new_ast(AST_BINARY_OP);
        node->op = TOK_OR;
        node->left = left;
        node->right = right;
        left = node;
    }

    return left;
}

static Ast *parse_expr(void) {
    return parse_or();
}

static Ast *parse_import(void) {
    Ast *imp = new_ast(AST_IMPORT);

    if (match(TOK_ADDON)) {
        imp->name = copy_token(consume(TOK_IDENT, "expected module name"));
        if (match(TOK_AS)) {
            imp->member = copy_token(consume(TOK_IDENT, "expected alias"));
        }
        return imp;
    }

    if (match(TOK_FROM)) {
        imp->name = copy_token(consume(TOK_IDENT, "expected module name"));
        consume(TOK_ADDEVERY, "expected 'addevery'");
        imp->bool_value = 1;
        return imp;
    }

    return NULL;
}

static Ast *parse_if(void) {
    consume(TOK_IF, "expected 'if'");
    Ast *ifstmt = new_ast(AST_IF);
    ifstmt->cond = parse_expr();
    ifstmt->body = parse_block();

    if (match(TOK_ELIF)) {
        Ast *elif = parse_if();
        ifstmt->else_branch = elif;
    } else if (match(TOK_ELSE)) {
        ifstmt->else_branch = parse_block();
    }

    return ifstmt;
}

static Ast *parse_while(void) {
    consume(TOK_WHILE, "expected 'while'");
    Ast *w = new_ast(AST_WHILE);
    w->cond = parse_expr();
    w->body = parse_block();
    return w;
}

static Ast *parse_for(void) {
    consume(TOK_FOR, "expected 'for'");
    Token var = consume(TOK_IDENT, "expected loop variable");
    consume(TOK_LPAREN, "expected '('");

    Ast *f = new_ast(AST_FOR);
    f->name = copy_token(var);
    f->init = parse_expr();

    consume(TOK_COMMA, "expected ','");
    f->cond = parse_expr();

    if (match(TOK_COMMA)) {
        f->update = parse_expr();
    }

    consume(TOK_RPAREN, "expected ')'");
    f->body = parse_block();

    return f;
}

static Ast *parse_funcdef(void) {
    consume(TOK_DEF, "expected 'def'");
    Ast *fn = new_ast(AST_FUNC_DEF);
    fn->name = copy_token(consume(TOK_IDENT, "expected function name"));

    consume(TOK_LPAREN, "expected '('");

    fn->params = calloc(16, sizeof(Ast *));
    fn->param_count = 0;

    if (!check(TOK_RPAREN)) {
        do {
            Token pname = consume(TOK_IDENT, "expected param name");
            consume(TOK_COLON, "expected ':'");
            Token ptype = consume(TOK_T_INT, "expected type");
            if (peek().type == TOK_T_FLT || peek().type == TOK_T_STR ||
                peek().type == TOK_T_BOOL || peek().type == TOK_T_PNT) {
                ptype = advance();
                }

                Ast *param = new_ast(AST_IDENT);
            param->name = copy_token(pname);
            param->return_type = copy_token(ptype);
            fn->params[fn->param_count++] = param;
        } while (match(TOK_COMMA));
    }

    consume(TOK_RPAREN, "expected ')'");

    if (match(TOK_ARROW)) {
        fn->return_type = copy_token(advance());
    }

    fn->body = parse_block();
    return fn;
}

static Ast *parse_stmt(void) {
    skip_newlines();

    if (check(TOK_IF)) {
        return parse_if();
    }

    if (check(TOK_WHILE)) {
        return parse_while();
    }

    if (check(TOK_FOR)) {
        return parse_for();
    }

    if (check(TOK_DEF)) {
        return parse_funcdef();
    }

    if (match(TOK_RETURN)) {
        Ast *ret = new_ast(AST_RETURN);
        if (!check(TOK_NL) && !check(TOK_RBRACE) && !check(TOK_EOF)) {
            ret->child = parse_expr();
        }
        return ret;
    }

    if (match(TOK_BREAK)) {
        return new_ast(AST_BREAK);
    }

    if (match(TOK_CONTINUE)) {
        return new_ast(AST_CONTINUE);
    }

    Ast *expr = parse_expr();
    if (!expr) return NULL;

    if (match(TOK_ASSIGN)) {
        Ast *assign = new_ast(AST_ASSIGN);
        assign->left = expr;
        assign->right = parse_expr();
        return assign;
    }

    if (match(TOK_PLUS_EQ) || match(TOK_MINUS_EQ) || match(TOK_STAR_EQ) ||
        match(TOK_SLASH_EQ) || match(TOK_SLASH2_EQ) || match(TOK_PCT2_EQ) || match(TOK_CARET_EQ)) {
        fprintf(stderr, "Compound assignments not yet supported\n");
        }

        Ast *stmt = new_ast(AST_EXPR_STMT);
    stmt->child = expr;
    return stmt;
}

static Ast *parse_block(void) {
    consume(TOK_LBRACE, "expected '{'");

    Ast *block = new_ast(AST_BLOCK);
    Ast *tail = NULL;

    skip_newlines();

    while (!check(TOK_RBRACE) && !check(TOK_EOF)) {
        Ast *stmt = parse_stmt();
        if (stmt) {
            if (!tail) {
                block->child = stmt;
            } else {
                tail->next = stmt;
            }
            tail = stmt;
        }

        skip_newlines();
    }

    consume(TOK_RBRACE, "expected '}'");

    return block;
}

Program *parse(const char *src) {
    Lexer lx;
    lexer_init(&lx, src);

    Token tokens[8192];
    int count = 0;

    Token err;
    for (;;) {
        Token t = lexer_next(&lx);
        if (t.type == TOK_ERROR) {
            fprintf(stderr, "Lex error at line %d col %d: %s\n", t.line, t.col, t.msg);
            return NULL;
        }
        tokens[count++] = t;
        if (t.type == TOK_EOF) break;
        if (count >= 8191) break;
    }

    parser.tokens = tokens;
    parser.count = count;
    parser.pos = 0;

    Program *prog = calloc(1, sizeof(Program));
    Ast *root = new_ast(AST_BLOCK);
    Ast *tail = NULL;

    skip_newlines();

    while (!check(TOK_EOF)) {
        if (check(TOK_ADDON) || check(TOK_FROM)) {
            Ast *imp = parse_import();
            if (imp) {
                if (!tail) {
                    root->child = imp;
                } else {
                    tail->next = imp;
                }
                tail = imp;
            }
        } else {
            Ast *stmt = parse_stmt();
            if (stmt) {
                if (!tail) {
                    root->child = stmt;
                } else {
                    tail->next = stmt;
                }
                tail = stmt;
            }
        }

        skip_newlines();
    }

    prog->root = root;
    return prog;
}

void ast_free(Ast *node) {
    if (!node) return;

    if (node->str_value) free(node->str_value);
    if (node->name) free(node->name);
    if (node->member) free(node->member);
    if (node->return_type) free(node->return_type);

    if (node->params) free(node->params);
    if (node->args) free(node->args);

    if (node->child) ast_free(node->child);
    if (node->left) ast_free(node->left);
    if (node->right) ast_free(node->right);
    if (node->cond) ast_free(node->cond);
    if (node->body) ast_free(node->body);
    if (node->else_branch) ast_free(node->else_branch);
    if (node->next) ast_free(node->next);

    free(node);
}
