#ifndef IO108_PARSER_H
#define IO108_PARSER_H

#include "lexer.h"

typedef enum {
    AST_PROGRAM, AST_IMPORT, AST_FUNC_DEF, AST_BLOCK, AST_IF, AST_WHILE, AST_FOR,
    AST_RETURN, AST_BREAK, AST_CONTINUE, AST_ASSIGN, AST_EXPR_STMT, AST_INT,
    AST_FLT, AST_STR, AST_BOOL, AST_IDENT, AST_CALL, AST_MEMBER_CALL,
    AST_BINARY_OP, AST_UNARY_OP, AST_LIST
} AstKind;

typedef struct Ast {
    AstKind kind;
    struct Ast *next;
    int int_value;
    double float_value;
    char *str_value;
    char *name;
    int bool_value;
    int is_pointer;
    struct Ast *cond, *body, *else_branch, *left, *right, *child;
    struct Ast **params;
    int param_count;
    char *return_type;
    struct Ast **args;
    int arg_count;
    char *member;
    TokenType op;
    struct Ast *init, *update;
} Ast;

typedef struct {
    Ast *root;
} Program;

Program *parse(const char *src);
void ast_free(Ast *node);

#endif
