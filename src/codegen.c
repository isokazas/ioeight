#include "codegen.h"

#include <complex.h>
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
    char *name;
    const char *type;
} VarInfo;

typedef struct {
    VarInfo vars[256];
    int count;
} VarSet;

static char *dupstr(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *copy = malloc(n);
    if (copy) memcpy(copy, s, n);
    return copy;
}

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
    vs->count = 0;
}

static int varset_has(VarSet *vs, const char *name) {
    for (int i = 0; i < vs->count; i++) {
        if (strcmp(vs->vars[i].name, name) == 0) return 1;
    }
    return 0;
}

static void varset_add_typed(VarSet *vs, const char *name, const char *type) {
    if (!name) return;
    if (strcmp(name, "int") == 0 || strcmp(name, "flt") == 0 ||
        strcmp(name, "str") == 0 || strcmp(name, "bool") == 0 ||
        strcmp(name, "pnt") == 0 || strcmp(name, "imgint") == 0 ||
        strcmp(name, "imgflt") == 0) return;

    for (int i = 0; i < vs->count; i++) {
        if (strcmp(vs->vars[i].name, name) == 0) {
            if (strcmp(vs->vars[i].type, "int") == 0 && strcmp(type, "int") != 0) {
                free(vs->vars[i].name);
                vs->vars[i].name = dupstr(name);
                vs->vars[i].type = type;
            }
            return;
        }
    }

    vs->vars[vs->count].name = dupstr(name);
    vs->vars[vs->count].type = type;
    vs->count++;
}

static const char *infer_expr_type(Ast *expr) {
    if (!expr) return "int";
    if (expr->kind == AST_STR) return "char *";
    if (expr->kind == AST_FLT) return "double";
    if (expr->kind == AST_BOOL) return "int";
    if (expr->kind == AST_LIST) return "int *";
    if (expr->kind == AST_CALL) {
        if (expr->left && expr->left->kind == AST_IDENT && expr->left->name) {
            if (strcmp(expr->left->name, "imgint") == 0 || strcmp(expr->left->name, "imgflt") == 0) {
                return "double complex";
            }
            if (strcmp(expr->left->name, "openfile") == 0) return "FILE *";
            if (strcmp(expr->left->name, "closefile") == 0) return "int";
        }
    }
    if (expr->kind == AST_UNARY_OP && expr->op == TOK_TILDA) {
        const char *child = infer_expr_type(expr->child);
        if (strcmp(child, "int") == 0) return "int *";
        if (strcmp(child, "double") == 0) return "double *";
        if (strcmp(child, "char *") == 0) return "char **";
        if (strcmp(child, "double complex") == 0) return "double complex *";
        return "void *";
    }
    if (expr->kind == AST_MEMBER_CALL && expr->left && expr->left->name && expr->member && strcmp(expr->member, "input") == 0) {
        const char *tname = expr->left->name;
        if (strcmp(tname, "str") == 0) return "char *";
        if (strcmp(tname, "flt") == 0) return "double";
        if (strcmp(tname, "int") == 0) return "int";
        if (strcmp(tname, "imgflt") == 0) return "double complex";
        if (strcmp(tname, "imgint") == 0) return "double complex";
    }
    return "int";
}

static const char *c_type(const char *io108_type) {
    if (!io108_type) return "int";
    if (strcmp(io108_type, "int") == 0) return "int";
    if (strcmp(io108_type, "flt") == 0) return "double";
    if (strcmp(io108_type, "str") == 0) return "char *";
    if (strcmp(io108_type, "bool") == 0) return "int";
    if (strcmp(io108_type, "pnt") == 0) return "void *";
    if (strcmp(io108_type, "imgint") == 0 || strcmp(io108_type, "imgflt") == 0) return "double complex";
    if (strcmp(io108_type, "imgint *") == 0 || strcmp(io108_type, "imgflt *") == 0) return "double complex *";
    if (strcmp(io108_type, "int *") == 0) return "int *";
    if (strcmp(io108_type, "double *") == 0) return "double *";
    if (strcmp(io108_type, "char *") == 0) return "char *";
    if (strcmp(io108_type, "FILE *") == 0) return "FILE *";
    if (strcmp(io108_type, "double complex") == 0) return "double complex";
    if (strcmp(io108_type, "double complex *") == 0) return "double complex *";
    return io108_type;
}

static void collect_vars(Ast *node, VarSet *vs, VarSet *funcs);

static void collect_vars_expr(Ast *expr, VarSet *vs, VarSet *funcs) {
    if (!expr) return;
    if (expr->kind == AST_IDENT && expr->name) {
        if (!varset_has(funcs, expr->name)) {
            varset_add_typed(vs, expr->name, "int");
        }
    }
    if (expr->left) collect_vars_expr(expr->left, vs, funcs);
    if (expr->right) collect_vars_expr(expr->right, vs, funcs);
    if (expr->child) collect_vars_expr(expr->child, vs, funcs);
}

static void collect_vars(Ast *node, VarSet *vs, VarSet *funcs) {
    if (!node) return;

    switch (node->kind) {
        case AST_ASSIGN:
            if (node->left && node->left->kind == AST_IDENT && node->left->name) {
                const char *t = infer_expr_type(node->right);
                varset_add_typed(vs, node->left->name, t);
            } else if (node->left && node->left->kind == AST_BINARY_OP && node->left->op == TOK_LBRACKET) {
                collect_vars_expr(node->left->left, vs, funcs);
                collect_vars_expr(node->left->right, vs, funcs);
            }
            collect_vars_expr(node->right, vs, funcs);
            break;
        case AST_FOR:
            if (node->name) varset_add_typed(vs, node->name, "int");
            collect_vars_expr(node->init, vs, funcs);
            collect_vars_expr(node->cond, vs, funcs);
            collect_vars_expr(node->update, vs, funcs);
            if (node->body) collect_vars(node->body, vs, funcs);
            break;
        case AST_WHILE:
            collect_vars_expr(node->cond, vs, funcs);
            if (node->body) collect_vars(node->body, vs, funcs);
            break;
        case AST_IF:
            collect_vars_expr(node->cond, vs, funcs);
            if (node->body) collect_vars(node->body, vs, funcs);
            if (node->else_branch) collect_vars(node->else_branch, vs, funcs);
            break;
        case AST_BLOCK:
            for (Ast *n = node->child; n; n = n->next) {
                collect_vars(n, vs, funcs);
            }
            break;
        case AST_EXPR_STMT:
            collect_vars_expr(node->child, vs, funcs);
            break;
        case AST_RETURN:
            collect_vars_expr(node->child, vs, funcs);
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

        case AST_ASSIGN: {
            if (stmt->left && stmt->left->kind == AST_IDENT && stmt->left->is_pointer) {
                const char *t = infer_expr_type(stmt->right);
                buf_printf(b, "%s %s = ", c_type(t), stmt->left->name ? stmt->left->name : "");
                emit_expr(b, stmt->right);
                buf_append(b, ";\n");
                break;
            }

            if (stmt->left && stmt->left->kind == AST_BINARY_OP && stmt->left->op == TOK_LBRACKET) {
                emit_expr(b, stmt->left->left);
                buf_append(b, "[");
                emit_expr(b, stmt->left->right);
                buf_append(b, "]");
            } else {
                emit_expr(b, stmt->left);
            }

            switch (stmt->op) {
                case TOK_ASSIGN: buf_append(b, " = "); break;
                case TOK_PLUS_EQ: buf_append(b, " += "); break;
                case TOK_MINUS_EQ: buf_append(b, " -= "); break;
                case TOK_STAR_EQ: buf_append(b, " *= "); break;
                case TOK_SLASH_EQ: buf_append(b, " /= "); break;
                case TOK_SLASH2_EQ: buf_append(b, " /= "); break;
                case TOK_PCT2_EQ: buf_append(b, " %= "); break;
                case TOK_CARET_EQ: buf_append(b, " = pow("); break;
                default: buf_append(b, " = "); break;
            }
            emit_expr(b, stmt->right);
            if (stmt->op == TOK_CARET_EQ) buf_append(b, ")");
            buf_append(b, ";\n");
            break;
        }

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

        case AST_FUNC_DEF: {
            buf_printf(b, "%s %s(", c_type(stmt->return_type), stmt->name);
            for (int i = 0; i < stmt->param_count; i++) {
                Ast *p = stmt->params[i];
                buf_printf(b, "%s %s", c_type(p->return_type), p->name);
                if (i + 1 < stmt->param_count) buf_append(b, ", ");
            }
            buf_append(b, ") {\n");

            VarSet func_vars;
            varset_init(&func_vars);
            VarSet funcs_dummy;
            varset_init(&funcs_dummy);
            collect_vars(stmt->body, &func_vars, &funcs_dummy);

            for (int i = 0; i < stmt->param_count; i++) {
                for (int j = 0; j < func_vars.count; j++) {
                    if (strcmp(func_vars.vars[j].name, stmt->params[i]->name) == 0) {
                        for (int k = j; k < func_vars.count - 1; k++) {
                            func_vars.vars[k] = func_vars.vars[k + 1];
                        }
                        func_vars.count--;
                        break;
                    }
                }
            }

            for (int i = 0; i < func_vars.count; i++) {
                buf_printf(b, "    %s %s = 0;\n", c_type(func_vars.vars[i].type), func_vars.vars[i].name);
            }
            if (func_vars.count > 0) buf_append(b, "\n");

            emit_stmt(b, stmt->body);
            buf_append(b, "}\n\n");
            break;
        }

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
        case AST_MEMBER_CALL:
            if (expr->left && expr->left->name && expr->member && strcmp(expr->member, "input") == 0) {
                const char *tname = expr->left->name;
                if (strcmp(tname, "int") == 0) {
                    buf_append(b, "({ int _res = 0; printf(");
                    if (expr->arg_count > 0) emit_expr(b, expr->args[0]); else buf_append(b, "\"\"");
                    buf_append(b, "); scanf(\"%d\", &_res); _res; })");
                    break;
                } else if (strcmp(tname, "flt") == 0) {
                    buf_append(b, "({ double _res = 0.0; printf(");
                    if (expr->arg_count > 0) emit_expr(b, expr->args[0]); else buf_append(b, "\"\"");
                    buf_append(b, "); scanf(\"%lf\", &_res); _res; })");
                    break;
                } else if (strcmp(tname, "str") == 0) {
                    buf_append(b, "({ char *_res = malloc(256); printf(");
                    if (expr->arg_count > 0) emit_expr(b, expr->args[0]); else buf_append(b, "\"\"");
                    buf_append(b, "); scanf(\"%255s\", _res); _res; })");
                    break;
                }
            }
            if (expr->left && expr->left->name) {
                buf_append(b, expr->left->name);
                buf_append(b, "_");
            }
            buf_append(b, expr->member ? expr->member : "");
            break;
        case AST_CALL: {
            if (expr->left && expr->left->kind == AST_IDENT && expr->left->name) {
                const char *fname = expr->left->name;
                if (strcmp(fname, "print") == 0) {
                    if (expr->arg_count > 0 && expr->args[0] && expr->args[0]->kind == AST_CALL && expr->args[0]->left && expr->args[0]->left->kind == AST_IDENT &&
                        (strcmp(expr->args[0]->left->name, "imgint") == 0 || strcmp(expr->args[0]->left->name, "imgflt") == 0)) {
                        buf_append(b, "printf(\"(%g%+gi)\\n\", creal((double complex)(");
                        emit_expr(b, expr->args[0]);
                        buf_append(b, ")), cimag((double complex)(");
                        emit_expr(b, expr->args[0]);
                        buf_append(b, "))) ");
                        break;
                    }
                    if (expr->arg_count > 0 && expr->args[0]->kind == AST_STR) {
                        buf_append(b, "printf(\"%s\\n\", ");
                        emit_expr(b, expr->args[0]);
                        buf_append(b, ")");
                    } else {
                        buf_append(b, "printf(\"%d\\n\", (int)(");
                        if (expr->arg_count > 0) emit_expr(b, expr->args[0]);
                        buf_append(b, "))");
                    }
                    break;
                }
                if (strcmp(fname, "sin") == 0) { buf_append(b, "sin("); ...; break; }
                if (strcmp(fname, "cos") == 0) { buf_append(b, "cos("); ...; break; }
                if (strcmp(fname, "tan") == 0) { buf_append(b, "tan("); ...; break; }
                if (strcmp(fname, "exp") == 0) { buf_append(b, "exp("); ...; break; }
                if (strcmp(fname, "sqrt") == 0) { buf_append(b, "sqrt("); ...; break; }
                if (strcmp(fname, "pow") == 0) { buf_append(b, "pow("); ...; break; }
                if (strcmp(fname, "openfile") == 0) { buf_append(b, "fopen("); ...; break; }
                if (strcmp(fname, "closefile") == 0) { buf_append(b, "fclose("); ...; break; }
                if (strcmp(fname, "imgint") == 0 || strcmp(fname, "imgflt") == 0) {
                    buf_append(b, "((double complex)((double)(");
                    if (expr->arg_count > 0) emit_expr(b, expr->args[0]);
                    buf_append(b, ") + (double)(");
                    if (expr->arg_count > 1) emit_expr(b, expr->args[1]);
                    buf_append(b, ") * I))");
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
        }
        case AST_BINARY_OP:
            if (expr->op == TOK_LBRACKET) {
                emit_expr(b, expr->left);
                buf_append(b, "[");
                emit_expr(b, expr->right);
                buf_append(b, "]");
                break;
            }
            if (expr->op == TOK_CARET) {
                buf_append(b, "pow((double)(");
                emit_expr(b, expr->left);
                buf_append(b, "), (double)(");
                emit_expr(b, expr->right);
                buf_append(b, "))");
                break;
            }
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
            else if (expr->op == TOK_TILDA) buf_append(b, "&");
            else if (expr->op == TOK_STAR) buf_append(b, "*");
            emit_expr(b, expr->child);
            buf_append(b, ")");
            break;
        case AST_LIST:
            buf_append(b, "(int[]){ ");
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

    buf_append(&b, "#include <complex.h>\n");
    buf_append(&b, "#include <math.h>\n");
    buf_append(&b, "#include <stdio.h>\n");
    buf_append(&b, "#include <stdlib.h>\n");
    buf_append(&b, "#include <string.h>\n\n");

    VarSet funcs;
    varset_init(&funcs);
    for (Ast *n = program->root->child; n; n = n->next) {
        if (n->kind == AST_FUNC_DEF && n->name) {
            varset_add_typed(&funcs, n->name, "int");
        }
    }

    buf_append(&b, "int main(void) {\n");

    VarSet main_vars;
    varset_init(&main_vars);

    for (Ast *n = program->root->child; n; n = n->next) {
        if (n->kind != AST_FUNC_DEF) {
            collect_vars(n, &main_vars, &funcs);
        }
    }

    for (int i = 0; i < main_vars.count; i++) {
        if (strstr(main_vars.vars[i].type, "*") == NULL && strcmp(main_vars.vars[i].type, "double complex") != 0) {
            buf_printf(&b, "    %s %s = 0;\n", c_type(main_vars.vars[i].type), main_vars.vars[i].name);
        } else if (strcmp(main_vars.vars[i].type, "double complex") == 0) {
            buf_printf(&b, "    %s %s = 0;\n", c_type(main_vars.vars[i].type), main_vars.vars[i].name);
        } else {
            buf_printf(&b, "    %s %s = NULL;\n", c_type(main_vars.vars[i].type), main_vars.vars[i].name);
        }
    }

    if (main_vars.count > 0) buf_append(&b, "\n");

    for (Ast *n = program->root->child; n; n = n->next) {
        if (n->kind == AST_FUNC_DEF) {
            emit_stmt(&b, n);
        }
    }

    for (Ast *n = program->root->child; n; n = n->next) {
        if (n->kind != AST_FUNC_DEF && n->kind != AST_IMPORT) {
            emit_stmt(&b, n);
        }
    }

    buf_append(&b, "    return 0;\n");
    buf_append(&b, "}\n");
    return b.buf;
}
