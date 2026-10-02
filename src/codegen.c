#include "codegen.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *buf;
    int len;
    int cap;
} Buffer;

typedef struct {
    char **names;
    int count;
} VarSet;

static void buf_init(Buffer *b) {
    b->cap = 4096;
    b->buf = malloc((size_t)b->cap);
    b->len = 0;
}

static void buf_append(Buffer *b, const char *s) {
    if (!s) return;
    size_t need = strlen(s);
    while (b->len + (int)need >= b->cap) {
        b->cap *= 2;
        b->buf = realloc(b->buf, (size_t)b->cap);
    }
    strcpy(b->buf + b->len, s);
    b->len += (int)need;
}

static void buf_printf(Buffer *b, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int need = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    while (b->len + need >= b->cap) {
        b->cap *= 2;
        b->buf = realloc(b->buf, (size_t)b->cap);
    }
    va_start(ap, fmt);
    vsnprintf(b->buf + b->len, (size_t)(need + 1), fmt, ap);
    va_end(ap);
    b->len += need;
}

static void varset_init(VarSet *vs) {
    vs->names = calloc(256, sizeof(char *));
    vs->count = 0;
}

static int varset_has(VarSet *vs, const char *name) {
    for (int i = 0; i < vs->count; i++) {
        if (strcmp(vs->names[i], name) == 0) return 1;
    }
    return 0;
}

static void varset_add(VarSet *vs, const char *name) {
    if (!varset_has(vs, name)) {
        vs->names[vs->count++] = malloc(strlen(name) + 1);
        strcpy(vs->names[vs->count - 1], name);
    }
}

static void varset_free(VarSet *vs) {
    for (int i = 0; i < vs->count; i++) {
        free(vs->names[i]);
    }
    free(vs->names);
}

static const char *c_type(const char *io108_type) {
    if (!io108_type) return "int";
    if (strcmp(io108_type, "int") == 0) return "int";
    if (strcmp(io108_type, "flt") == 0) return "double";
    if (strcmp(io108_type, "str") == 0) return "char *";
    if (strcmp(io108_type, "bool") == 0) return "int";
    if (strcmp(io108_type, "pnt") == 0) return "void *";
    return "int";
}

static void collect_vars(Ast *node, VarSet *vs);

static void collect_vars_expr(Ast *expr, VarSet *vs) {
    if (!expr) return;
    if (expr->kind == AST_IDENT && expr->name) {
        varset_add(vs, expr->name);
    }
    if (expr->left) collect_vars_expr(expr->left, vs);
    if (expr->right) collect_vars_expr(expr->right, vs);
    if (expr->child) collect_vars_expr(expr->child, vs);
}

static void collect_vars(Ast *node, VarSet *vs) {
    if (!node) return;

    switch (node->kind) {
    case AST_ASSIGN:
        if (node->left && node->left->kind == AST_IDENT && node->left->name) {
            varset_add(vs, node->left->name);
        }
        collect_vars_expr(node->right, vs);
        break;
    case AST_FOR:
        if (node->name) varset_add(vs, node->name);
        collect_vars_expr(node->init, vs);
        collect_vars_expr(node->cond, vs);
        collect_vars_expr(node->update, vs);
        if (node->body) collect_vars(node->body, vs);
        break;
    case AST_WHILE:
        collect_vars_expr(node->cond, vs);
        if (node->body) collect_vars(node->body, vs);
        break;
    case AST_IF:
        collect_vars_expr(node->cond, vs);
        if (node->body) collect_vars(node->body, vs);
        if (node->else_branch) collect_vars(node->else_branch, vs);
        break;
    case AST_BLOCK:
        for (Ast *n = node->child; n; n = n->next) {
            collect_vars(n, vs);
        }
        break;
    case AST_EXPR_STMT:
        collect_vars_expr(node->child, vs);
        break;
    case AST_RETURN:
        collect_vars_expr(node->child, vs);
        break;
    default:
        break;
    }
}

static void emit_expr(Buffer *b, Ast *expr);

static void emit_stmt(Buffer *b, Ast *stmt) {
    if (!stmt) return;

    switch (stmt->kind) {
    case AST_BLOCK:
        for (Ast *n = stmt->child; n; n = n->next) {
            emit_stmt(b, n);
        }
        break;

    case AST_EXPR_STMT:
        emit_expr(b, stmt->child);
        buf_append(b, ";\n");
        break;

    case AST_ASSIGN:
        emit_expr(b, stmt->left);
        buf_append(b, " = ");
        emit_expr(b, stmt->right);
        buf_append(b, ";\n");
        break;

    case AST_RETURN:
        buf_append(b, "return");
        if (stmt->child) {
            buf_append(b, " ");
            emit_expr(b, stmt->child);
        }
        buf_append(b, ";\n");
        break;

    case AST_IF:
        buf_append(b, "if (");
        emit_expr(b, stmt->cond);
        buf_append(b, ") {\n");
        emit_stmt(b, stmt->body);
        buf_append(b, "}\n");
        if (stmt->else_branch) {
            if (stmt->else_branch->kind == AST_IF) {
                buf_append(b, "else ");
                emit_stmt(b, stmt->else_branch);
            } else {
                buf_append(b, "else {\n");
                emit_stmt(b, stmt->else_branch);
                buf_append(b, "}\n");
            }
        }
        break;

    case AST_WHILE:
        buf_append(b, "while (");
        emit_expr(b, stmt->cond);
        buf_append(b, ") {\n");
        emit_stmt(b, stmt->body);
        buf_append(b, "}\n");
        break;

    case AST_FOR:
        buf_printf(b, "for (int %s = ", stmt->name);
        emit_expr(b, stmt->init);
        buf_append(b, "; ");
        buf_printf(b, "%s < ", stmt->name);
        emit_expr(b, stmt->cond);
        buf_append(b, "; ");
        if (stmt->update) {
            buf_printf(b, "%s += ", stmt->name);
            emit_expr(b, stmt->update);
        } else {
            buf_printf(b, "%s++", stmt->name);
        }
        buf_append(b, ") {\n");
        emit_stmt(b, stmt->body);
        buf_append(b, "}\n");
        break;

    case AST_FUNC:
        buf_printf(b, "%s %s(", c_type(stmt->return_type), stmt->name);
        for (int i = 0; i < stmt->param_count; i++) {
            Ast *p = stmt->params[i];
            buf_printf(b, "%s %s", c_type(p->return_type), p->name);
            if (i + 1 < stmt->param_count) buf_append(b, ", ");
        }
        buf_append(b, ") {\n");
        emit_stmt(b, stmt->body);
        buf_append(b, "}\n\n");
        break;

    case AST_BREAK:
        buf_append(b, "break;\n");
        break;

    case AST_CONTINUE:
        buf_append(b, "continue;\n");
        break;

    case AST_IMPORT:
        buf_append(b, "/* import ");
        if (stmt->name) buf_append(b, stmt->name);
        buf_append(b, " */\n");
        break;

    default:
        break;
    }
}

static void emit_expr(Buffer *b, Ast *expr) {
    if (!expr) return;

    switch (expr->kind) {
    case AST_INT:
        buf_printf(b, "%d", expr->int_value);
        break;
    case AST_FLT:
        buf_printf(b, "%g", expr->float_value);
        break;
    case AST_STR:
        buf_append(b, "\"");
        if (expr->str_value) {
            for (int i = 0; expr->str_value[i] != '\0'; i++) {
                if (expr->str_value[i] == '"') buf_append(b, "\\\"");
                else if (expr->str_value[i] == '\\') buf_append(b, "\\\\");
                else buf_printf(b, "%c", expr->str_value[i]);
            }
        }
        buf_append(b, "\"");
        break;
    case AST_BOOL:
        buf_append(b, expr->bool_value ? "1" : "0");
        break;
    case AST_IDENT:
        buf_append(b, expr->name ? expr->name : "");
        break;
    case AST_MEMBER:
        if (expr->left && expr->left->name) {
            buf_append(b, expr->left->name);
            buf_append(b, "_");
        }
        buf_append(b, expr->member ? expr->member : "");
        break;
    case AST_CALL:
        if (expr->left && expr->left->kind == AST_IDENT) {
            const char *fname = expr->left->name;
            if (fname && strcmp(fname, "print") == 0) {
                buf_append(b, "printf(\"%d\\n\", (int)(");
                if (expr->arg_count > 0) emit_expr(b, expr->args[0]);
                buf_append(b, "))");
                break;
            }
        }
        if (expr->left) {
            emit_expr(b, expr->left);
            buf_append(b, "(");
            for (int i = 0; i < expr->arg_count; i++) {
                emit_expr(b, expr->args[i]);
                if (i + 1 < expr->arg_count) buf_append(b, ", ");
            }
            buf_append(b, ")");
        }
        break;
    case AST_BINARY_OP:
        buf_append(b, "(");
        emit_expr(b, expr->left);
        buf_append(b, " ");
        switch (expr->op) {
        case TOK_PLUS: buf_append(b, "+"); break;
        case TOK_MINUS: buf_append(b, "-"); break;
        case TOK_STAR: buf_append(b, "*"); break;
        case TOK_SLASH: buf_append(b, "/"); break;
        case TOK_SLASH2: buf_append(b, "/"); break;
        case TOK_PCT2: buf_append(b, "%"); break;
        case TOK_CARET: buf_append(b, "^"); break;
        case TOK_EQ: buf_append(b, "=="); break;
        case TOK_NEQ: buf_append(b, "!="); break;
        case TOK_LT: buf_append(b, "<"); break;
        case TOK_LE: buf_append(b, "<="); break;
        case TOK_GT: buf_append(b, ">"); break;
        case TOK_GE: buf_append(b, ">="); break;
        case TOK_AND: buf_append(b, "&&"); break;
        case TOK_OR: buf_append(b, "||"); break;
        default: buf_append(b, "?"); break;
        }
        buf_append(b, " ");
        emit_expr(b, expr->right);
        buf_append(b, ")");
        break;
    case AST_UNARY_OP:
        buf_append(b, "(");
        if (expr->op == TOK_MINUS) buf_append(b, "-");
        else if (expr->op == TOK_NOT) buf_append(b, "!");
        emit_expr(b, expr->child);
        buf_append(b, ")");
        break;
    case AST_LIST:
        buf_append(b, "{ ");
        for (int i = 0; i < expr->arg_count; i++) {
            emit_expr(b, expr->args[i]);
            if (i + 1 < expr->arg_count) buf_append(b, ", ");
        }
        buf_append(b, " }");
        break;
    default:
        break;
    }
}

char *generate_c(Program *program) {
    Buffer b;
    buf_init(&b);

    buf_append(&b, "#include <stdio.h>\n");
    buf_append(&b, "#include <stdlib.h>\n");
    buf_append(&b, "#include <string.h>\n\n");

    buf_append(&b, "int main(void) {\n");

    VarSet main_vars;
    varset_init(&main_vars);

    for (Ast *n = program->root->child; n; n = n->next) {
        if (n->kind != AST_FUNC) {
            collect_vars(n, &main_vars);
        }
    }

    for (int i = 0; i < main_vars.count; i++) {
        buf_printf(&b, "    int %s = 0;\n", main_vars.names[i]);
    }

    if (main_vars.count > 0) buf_append(&b, "\n");

    for (Ast *n = program->root->child; n; n = n->next) {
        if (n->kind == AST_FUNC) {
            emit_stmt(&b, n);
        }
    }

    for (Ast *n = program->root->child; n; n = n->next) {
        if (n->kind != AST_FUNC && n->kind != AST_IMPORT) {
            emit_stmt(&b, n);
        }
    }

    buf_append(&b, "    return 0;\n");
    buf_append(&b, "}\n");

    varset_free(&main_vars);
    return b.buf;
}
