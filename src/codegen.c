#include "codegen.h"

#include <complex.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} Buffer;

typedef struct {
    char *name;
    const char *type;
} Var;

typedef struct {
    Var vars[256];
    int count;
} VarSet;

static int reserve(Buffer *b, size_t extra) {
    if (extra > (size_t)-1 - b->len - 1) return 0;
    size_t need = b->len + extra + 1;
    if (need <= b->cap) return 1;
    size_t cap = b->cap ? b->cap : 1024;
    while (cap < need) {
        if (cap > (size_t)-1 / 2) { cap = need; break; }
        cap *= 2;
    }
    char *p = realloc(b->data, cap);
    if (!p) return 0;
    b->data = p;
    b->cap = cap;
    return 1;
}

static void append(Buffer *b, const char *s) {
    if (!s) return;
    size_t n = strlen(s);
    if (!reserve(b, n)) return;
    memcpy(b->data + b->len, s, n + 1);
    b->len += n;
}

static void printbuf(Buffer *b, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0 || !reserve(b, (size_t)n)) return;
    va_start(ap, fmt);
    vsnprintf(b->data + b->len, b->cap - b->len, fmt, ap);
    va_end(ap);
    b->len += (size_t)n;
}

static const char *c_type(const char *type) {
    if (!type) return "int";
    if (!strcmp(type, "int") || !strcmp(type, "bool")) return "int";
    if (!strcmp(type, "flt") || !strcmp(type, "double")) return "double";
    if (!strcmp(type, "str") || !strcmp(type, "char *")) return "char *";
    if (!strcmp(type, "pnt") || !strcmp(type, "void *")) return "void *";
    if (!strcmp(type, "imgint") || !strcmp(type, "imgflt") || !strcmp(type, "double complex")) return "double complex";
    if (!strcmp(type, "int *")) return "int *";
    if (!strcmp(type, "double *")) return "double *";
    if (!strcmp(type, "char **")) return "char **";
    if (!strcmp(type, "double complex *")) return "double complex *";
    if (!strcmp(type, "FILE *")) return "FILE *";
    return type;
}

static const char *infer_type(Ast *e) {
    if (!e) return "int";
    switch (e->kind) {
    case AST_FLT: return "double";
    case AST_STR: return "char *";
    case AST_BOOL: case AST_INT: return "int";
    case AST_LIST: return "int *";
    case AST_CALL:
        if (e->left && e->left->kind == AST_IDENT && e->left->name) {
            if (!strcmp(e->left->name, "imgint") || !strcmp(e->left->name, "imgflt")) return "double complex";
            if (!strcmp(e->left->name, "openfile")) return "FILE *";
        }
        break;
    case AST_UNARY_OP:
        if (e->op == TOK_TILDA) {
            const char *child = infer_type(e->child);
            if (!strcmp(child, "double")) return "double *";
            if (!strcmp(child, "double complex")) return "double complex *";
            if (!strcmp(child, "char *")) return "char **";
            if (!strcmp(child, "void *")) return "void **";
            return "int *";
        }
        if (e->op == TOK_STAR) return "int";
        break;
    default: break;
    }
    return "int";
}

static int var_index(VarSet *set, const char *name) {
    for (int i = 0; i < set->count; i++)
        if (!strcmp(set->vars[i].name, name)) return i;
    return -1;
}

static void add_var(VarSet *set, const char *name, const char *type) {
    if (!name || !*name) return;
    int i = var_index(set, name);
    if (i >= 0) {
        if (!set->vars[i].type || !strcmp(set->vars[i].type, "int")) set->vars[i].type = type;
        return;
    }
    if (set->count >= 256) return;
    char *copy = malloc(strlen(name) + 1);
    if (!copy) return;
    strcpy(copy, name);
    set->vars[set->count].name = copy;
    set->vars[set->count].type = type;
    set->count++;
}

static void free_vars(VarSet *set) {
    for (int i = 0; i < set->count; i++) free(set->vars[i].name);
}

static int is_builtin(const char *name) {
    static const char *builtins[] = {
        "print", "sin", "cos", "tan", "asin", "acos", "atan", "sinh", "cosh", "tanh",
        "exp", "log", "log10", "sqrt", "pow", "fabs", "openfile", "closefile",
        "fprintf", "fread", "fwrite", "imgint", "imgflt", "int", "flt", "str", "bool", "pnt"
    };
    for (size_t i = 0; i < sizeof(builtins) / sizeof(builtins[0]); i++)
        if (!strcmp(name, builtins[i])) return 1;
    return 0;
}

static void collect_expr(Ast *e, VarSet *set) {
    if (!e) return;
    if (e->kind == AST_IDENT && e->name && !is_builtin(e->name)) add_var(set, e->name, "int");
    collect_expr(e->left, set);
    collect_expr(e->right, set);
    collect_expr(e->child, set);
    for (int i = 0; i < e->arg_count; i++) collect_expr(e->args[i], set);
}

static void collect_stmts(Ast *n, VarSet *set) {
    for (; n; n = n->next) {
        switch (n->kind) {
        case AST_ASSIGN:
            if (n->left && n->left->kind == AST_IDENT)
                add_var(set, n->left->name, n->left->is_pointer ? infer_type(n->right) : infer_type(n->right));
            collect_expr(n->left, set);
            collect_expr(n->right, set);
            break;
        case AST_IF:
            collect_expr(n->cond, set);
            if (n->body) collect_stmts(n->body->child, set);
            if (n->else_branch) {
                if (n->else_branch->kind == AST_BLOCK) collect_stmts(n->else_branch->child, set);
                else collect_stmts(n->else_branch, set);
            }
            break;
        case AST_WHILE:
            collect_expr(n->cond, set);
            if (n->body) collect_stmts(n->body->child, set);
            break;
        case AST_FOR:
            add_var(set, n->name, "int");
            collect_expr(n->init, set); collect_expr(n->cond, set); collect_expr(n->update, set);
            if (n->body) collect_stmts(n->body->child, set);
            break;
        case AST_BLOCK: collect_stmts(n->child, set); break;
        case AST_EXPR_STMT: collect_expr(n->child, set); break;
        case AST_RETURN: collect_expr(n->child, set); break;
        default: break;
        }
    }
}

static void emit_expr(Buffer *b, Ast *e);
static void emit_stmt(Buffer *b, Ast *n);

static void emit_args(Buffer *b, Ast *call) {
    for (int i = 0; i < call->arg_count; i++) {
        if (i) append(b, ", ");
        emit_expr(b, call->args[i]);
    }
}

static void emit_expr(Buffer *b, Ast *e) {
    if (!e) return;
    switch (e->kind) {
    case AST_INT: printbuf(b, "%d", e->int_value); break;
    case AST_FLT: printbuf(b, "%.17g", e->float_value); break;
    case AST_STR: append(b, e->str_value ? e->str_value : "\"\""); break;
    case AST_BOOL: append(b, e->bool_value ? "1" : "0"); break;
    case AST_IDENT: append(b, e->name ? e->name : ""); break;
    case AST_LIST:
        append(b, "(int[]){ "); emit_args(b, e); append(b, " }"); break;
    case AST_MEMBER_CALL:
        if (e->left && e->left->kind == AST_IDENT && e->member && !strcmp(e->member, "input")) {
            const char *type = e->left->name;
            if (!strcmp(type, "int")) {
                append(b, "({ int _v=0; printf("); if (e->arg_count) emit_expr(b,e->args[0]); else append(b,"\"\""); append(b, "); scanf(\"%d\", &_v); _v; })");
            } else if (!strcmp(type, "flt")) {
                append(b, "({ double _v=0; printf("); if (e->arg_count) emit_expr(b,e->args[0]); else append(b,"\"\""); append(b, "); scanf(\"%lf\", &_v); _v; })");
            } else append(b, "0");
        } else {
            emit_expr(b, e->left); append(b, "_"); append(b, e->member ? e->member : "");
        }
        break;
    case AST_CALL: {
        const char *name = e->left && e->left->kind == AST_IDENT ? e->left->name : NULL;
        if (name && !strcmp(name, "print")) {
            Ast *arg = e->arg_count ? e->args[0] : NULL;
            if (arg && arg->kind == AST_STR) { append(b, "printf(\"%s\\n\", "); emit_expr(b,arg); append(b,")"); }
            else if (arg && arg->kind == AST_CALL && arg->left && arg->left->kind == AST_IDENT &&
                     (!strcmp(arg->left->name,"imgint") || !strcmp(arg->left->name,"imgflt"))) {
                append(b,"printf(\"(%g%+gi)\\n\", creal("); emit_expr(b,arg); append(b,"), cimag("); emit_expr(b,arg); append(b,"))");
            } else if (arg && !strcmp(infer_type(arg),"double")) { append(b,"printf(\"%g\\n\", (double)("); emit_expr(b,arg); append(b,"))"); }
            else { append(b,"printf(\"%d\\n\", (int)("); if(arg) emit_expr(b,arg); else append(b,"0"); append(b,"))"); }
            break;
        }
        if (name && (!strcmp(name,"imgint") || !strcmp(name,"imgflt"))) {
            append(b,"((double complex)((double)("); if(e->arg_count)emit_expr(b,e->args[0]); append(b,") + (double)("); if(e->arg_count>1)emit_expr(b,e->args[1]); append(b,") * I))"); break;
        }
        if (name && !strcmp(name,"openfile")) append(b,"fopen");
        else if (name && !strcmp(name,"closefile")) append(b,"fclose");
        else if (name) append(b,name);
        else emit_expr(b,e->left);
        append(b,"("); emit_args(b,e); append(b,")");
        break;
    }
    case AST_BINARY_OP:
        if (e->op == TOK_LBRACKET) { emit_expr(b,e->left); append(b,"["); emit_expr(b,e->right); append(b,"]"); break; }
        if (e->op == TOK_CARET) { append(b,"pow(");emit_expr(b,e->left);append(b,", ");emit_expr(b,e->right);append(b,")");break; }
        append(b,"("); emit_expr(b,e->left); append(b," ");
        switch(e->op) {
        case TOK_PLUS:append(b,"+");break; case TOK_MINUS:append(b,"-");break; case TOK_STAR:append(b,"*");break;
        case TOK_SLASH:case TOK_SLASH2:append(b,"/");break;case TOK_PCT2:append(b,"%");break;
        case TOK_EQ:append(b,"==");break;case TOK_NEQ:append(b,"!=");break;case TOK_LT:append(b,"<");break;
        case TOK_LE:append(b,"<=");break;case TOK_GT:append(b,">");break;case TOK_GE:append(b,">=");break;
        case TOK_AND:append(b,"&&");break;case TOK_OR:append(b,"||");break;default:append(b,"?");break;
        }
        append(b," ");emit_expr(b,e->right);append(b,")");break;
    case AST_UNARY_OP:
        append(b,"(");
        if(e->op==TOK_MINUS)append(b,"-");else if(e->op==TOK_NOT)append(b,"!");else if(e->op==TOK_TILDA)append(b,"&");else if(e->op==TOK_STAR)append(b,"*");
        emit_expr(b,e->child);append(b,")");break;
    default: break;
    }
}

static void emit_stmt(Buffer *b, Ast *n) {
    for (; n; n = n->next) {
        switch(n->kind) {
        case AST_BLOCK: emit_stmt(b,n->child); break;
        case AST_EXPR_STMT: append(b,"    ");emit_expr(b,n->child);append(b,";\n");break;
        case AST_ASSIGN:
            append(b,"    ");
            if(n->left && n->left->kind==AST_IDENT && n->left->is_pointer) {
                /* The pointer was declared with the other locals; initialize it here. */
                emit_expr(b,n->left);append(b," = ");emit_expr(b,n->right);append(b,";\n");break;
            }
            emit_expr(b,n->left);append(b," = ");emit_expr(b,n->right);append(b,";\n");break;
        case AST_RETURN: append(b,"    return");if(n->child){append(b," ");emit_expr(b,n->child);}append(b,";\n");break;
        case AST_BREAK: append(b,"    break;\n");break;
        case AST_CONTINUE: append(b,"    continue;\n");break;
        case AST_IF:
            append(b,"    if (");emit_expr(b,n->cond);append(b,") {\n");if(n->body)emit_stmt(b,n->body->child);append(b,"    }\n");
            if(n->else_branch){append(b,"    else {\n");if(n->else_branch->kind==AST_BLOCK)emit_stmt(b,n->else_branch->child);else emit_stmt(b,n->else_branch);append(b,"    }\n");}break;
        case AST_WHILE: append(b,"    while (");emit_expr(b,n->cond);append(b,") {\n");if(n->body)emit_stmt(b,n->body->child);append(b,"    }\n");break;
        case AST_FOR:
            printbuf(b,"    for (int %s = ",n->name);emit_expr(b,n->init);printbuf(b,"; %s < ",n->name);emit_expr(b,n->cond);printbuf(b,"; %s++ ) {\n",n->name);if(n->body)emit_stmt(b,n->body->child);append(b,"    }\n");break;
        default: break;
        }
    }
}

static void emit_function(Buffer *b, Ast *fn) {
    printbuf(b,"static %s %s(",c_type(fn->return_type),fn->name ? fn->name : "function");
    for(int i=0;i<fn->param_count;i++) {
        if(i)append(b,", ");
        printbuf(b,"%s %s",c_type(fn->params[i]->return_type),fn->params[i]->name);
    }
    if(fn->param_count==0)append(b,"void");
    append(b,") {\n");
    VarSet locals={0};
    if(fn->body)collect_stmts(fn->body->child,&locals);
    for(int i=0;i<fn->param_count;i++) {
        int p=var_index(&locals,fn->params[i]->name);
        if(p>=0) { free(locals.vars[p].name); for(int j=p;j<locals.count-1;j++)locals.vars[j]=locals.vars[j+1]; locals.count--; }
    }
    for(int i=0;i<locals.count;i++) printbuf(b,"    %s %s = %s;\n",c_type(locals.vars[i].type),locals.vars[i].name,strchr(c_type(locals.vars[i].type),'*')?"NULL":"0");
    if(locals.count)append(b,"\n");
    if(fn->body)emit_stmt(b,fn->body->child);
    append(b,"}\n\n");
    free_vars(&locals);
}

char *generate_c(Program *program) {
    if(!program || !program->root) return NULL;
    Buffer b={0};
    append(&b,"#include <complex.h>\n#include <math.h>\n#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n\n");
    for(Ast *n=program->root->child;n;n=n->next) if(n->kind==AST_FUNC_DEF) emit_function(&b,n);
    append(&b,"int main(void) {\n");
    VarSet vars={0};
    for(Ast *n=program->root->child;n;n=n->next) if(n->kind!=AST_FUNC_DEF && n->kind!=AST_IMPORT) collect_stmts(n,&vars);
    for(int i=0;i<vars.count;i++) {
        const char *t=c_type(vars.vars[i].type);
        printbuf(&b,"    %s %s = %s;\n",t,vars.vars[i].name,strchr(t,'*')?"NULL":"0");
    }
    if(vars.count)append(&b,"\n");
    for(Ast *n=program->root->child;n;n=n->next) if(n->kind!=AST_FUNC_DEF && n->kind!=AST_IMPORT) emit_stmt(&b,n);
    append(&b,"    return 0;\n}\n");
    free_vars(&vars);
    return b.data;
}
