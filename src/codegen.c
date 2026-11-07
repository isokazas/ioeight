#include "codegen.h"

#include <complex.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { char *data; size_t len, cap; } Buffer;
typedef struct { char *name; const char *type; } Var;
typedef struct { Var vars[256]; int count; } VarSet;

static void append(Buffer *b, const char *s) {
    size_t n = strlen(s);
    if (b->len + n + 1 > b->cap) {
        size_t cap = b->cap ? b->cap : 1024;
        while (cap < b->len + n + 1) cap *= 2;
        char *p = realloc(b->data, cap);
        if (!p) return;
        b->data = p; b->cap = cap;
    }
    memcpy(b->data + b->len, s, n + 1);
    b->len += n;
}

static void printbuf(Buffer *b, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int n = vsnprintf(NULL, 0, fmt, ap); va_end(ap);
    if (n < 0) return;
    char *tmp = malloc((size_t)n + 1);
    if (!tmp) return;
    va_start(ap, fmt); vsnprintf(tmp, (size_t)n + 1, fmt, ap); va_end(ap);
    append(b, tmp); free(tmp);
}

static const char *expr_type(Ast *e) {
    if (!e) return "int";
    if (e->kind == AST_FLT) return "double";
    if (e->kind == AST_STR) return "char *";
    if (e->kind == AST_BOOL || e->kind == AST_INT) return "int";
    if (e->kind == AST_CALL && e->left && e->left->kind == AST_IDENT) {
        const char *n = e->left->name;
        if (!strcmp(n,"imgint") || !strcmp(n,"imgflt")) return "double complex";
        if (!strcmp(n,"openfile")) return "FILE *";
    }
    if (e->kind == AST_UNARY_OP && e->op == TOK_TILDA) {
        const char *t = expr_type(e->child);
        if (!strcmp(t,"double")) return "double *";
        if (!strcmp(t,"double complex")) return "double complex *";
        if (!strcmp(t,"char *")) return "char **";
        return "int *";
    }
    return "int";
}

static int has(VarSet *v, const char *name) {
    for (int i=0;i<v->count;i++) if (!strcmp(v->vars[i].name,name)) return i+1;
    return 0;
}
static void add(VarSet *v, const char *name, const char *type) {
    if (!name || !*name || has(v,name) || v->count >= 256) return;
    v->vars[v->count].name = strdup(name);
    v->vars[v->count].type = type;
    v->count++;
}
static void freevars(VarSet *v) {
    for (int i=0;i<v->count;i++) free(v->vars[i].name);
}
static int builtin(const char *s) {
    static const char *names[] = {"print","sin","cos","tan","asin","acos","atan","exp","log","sqrt","pow","openfile","closefile","fprintf","imgint","imgflt","int","flt","str","bool","pnt"};
    for (size_t i=0;i<sizeof(names)/sizeof(names[0]);i++) if (!strcmp(s,names[i])) return 1;
    return 0;
}
static void collect_expr(Ast *e, VarSet *v) {
    if (!e) return;
    if (e->kind == AST_IDENT && e->name && !builtin(e->name)) add(v,e->name,"int");
    collect_expr(e->left,v); collect_expr(e->right,v); collect_expr(e->child,v);
    for (int i=0;i<e->arg_count;i++) collect_expr(e->args[i],v);
}
static void collect(Ast *n, VarSet *v) {
    for (;n;n=n->next) {
        if (n->kind == AST_ASSIGN) {
            if (n->left && n->left->kind == AST_IDENT) {
                const char *t = n->left->is_pointer ? expr_type(n->right) : expr_type(n->right);
                add(v,n->left->name,t);
            }
            collect_expr(n->left,v); collect_expr(n->right,v);
        } else if (n->kind == AST_IF) {
            collect_expr(n->cond,v); collect(n->body ? n->body->child : NULL,v); collect(n->else_branch,v);
        } else if (n->kind == AST_WHILE) {
            collect_expr(n->cond,v); collect(n->body ? n->body->child : NULL,v);
        } else if (n->kind == AST_BLOCK) collect(n->child,v);
        else if (n->kind == AST_EXPR_STMT) collect_expr(n->child,v);
        else if (n->kind == AST_RETURN) collect_expr(n->child,v);
    }
}
static const char *ctype(const char *t) {
    if (!strcmp(t,"FILE *")) return "FILE *";
    if (!strcmp(t,"double complex *")) return "double complex *";
    if (!strcmp(t,"double *")) return "double *";
    if (!strcmp(t,"char **")) return "char **";
    if (!strcmp(t,"char *")) return "char *";
    if (!strcmp(t,"double complex")) return "double complex";
    if (!strcmp(t,"double")) return "double";
    return "int";
}
static void emit_expr(Buffer *, Ast *);
static void emit_stmt(Buffer *, Ast *);

static void emit_args(Buffer *b, Ast *e) {
    for (int i=0;i<e->arg_count;i++) { if(i) append(b,", "); emit_expr(b,e->args[i]); }
}
static void emit_expr(Buffer *b, Ast *e) {
    if (!e) return;
    switch(e->kind) {
    case AST_INT: printbuf(b,"%d",e->int_value); break;
    case AST_FLT: printbuf(b,"%.17g",e->float_value); break;
    case AST_STR: append(b,e->str_value ? e->str_value : "\"\""); break;
    case AST_BOOL: append(b,e->bool_value ? "1" : "0"); break;
    case AST_IDENT: append(b,e->name ? e->name : ""); break;
    case AST_LIST:
        append(b,"(int[]){"); emit_args(b,e); append(b,"}"); break;
    case AST_MEMBER_CALL:
        if (e->left && e->left->name && e->member && !strcmp(e->member,"input")) {
            if (!strcmp(e->left->name,"int")) {
                append(b,"({ int _v=0; printf("); if(e->arg_count) emit_expr(b,e->args[0]); else append(b,"\"\""); append(b,"); scanf(\"%d\", &_v); _v; })");
            } else if (!strcmp(e->left->name,"flt")) {
                append(b,"({ double _v=0; printf("); if(e->arg_count) emit_expr(b,e->args[0]); else append(b,"\"\""); append(b,"); scanf(\"%lf\", &_v); _v; })");
            }
        } else { emit_expr(b,e->left); append(b,"_"); append(b,e->member ? e->member : ""); }
        break;
    case AST_CALL: {
        const char *name = e->left && e->left->kind==AST_IDENT ? e->left->name : NULL;
        if (name && !strcmp(name,"imgint")) {
            append(b,"((double complex)((double)("); if(e->arg_count) emit_expr(b,e->args[0]); append(b,") + (double)("); if(e->arg_count>1) emit_expr(b,e->args[1]); append(b,") * I))"); break;
        }
        if (name && !strcmp(name,"imgflt")) {
            append(b,"((double complex)((double)("); if(e->arg_count) emit_expr(b,e->args[0]); append(b,") + (double)("); if(e->arg_count>1) emit_expr(b,e->args[1]); append(b,") * I))"); break;
        }
        if (name && !strcmp(name,"openfile")) append(b,"fopen");
        else if (name && !strcmp(name,"closefile")) append(b,"fclose");
        else if (name) append(b,name);
        else emit_expr(b,e->left);
        append(b,"("); emit_args(b,e); append(b,")"); break;
    }
    case AST_BINARY_OP:
        if(e->op==TOK_LBRACKET) { emit_expr(b,e->left); append(b,"["); emit_expr(b,e->right); append(b,"]"); break; }
        if(e->op==TOK_CARET) { append(b,"pow("); emit_expr(b,e->left); append(b,", "); emit_expr(b,e->right); append(b,")"); break; }
        append(b,"("); emit_expr(b,e->left); append(b," ");
        switch(e->op) {
        case TOK_PLUS:append(b,"+");break; case TOK_MINUS:append(b,"-");break; case TOK_STAR:append(b,"*");break;
        case TOK_SLASH:case TOK_SLASH2:append(b,"/");break; case TOK_PCT2:append(b,"%");break;
        case TOK_EQ:append(b,"==");break;case TOK_NEQ:append(b,"!=");break;case TOK_LT:append(b,"<");break;
        case TOK_LE:append(b,"<=");break;case TOK_GT:append(b,">");break;case TOK_GE:append(b,">=");break;
        case TOK_AND:append(b,"&&");break;case TOK_OR:append(b,"||");break;default:append(b,"?");break;
        }
        append(b," "); emit_expr(b,e->right); append(b,")"); break;
    case AST_UNARY_OP:
        append(b,"("); if(e->op==TOK_MINUS) append(b,"-"); else if(e->op==TOK_NOT) append(b,"!"); else if(e->op==TOK_TILDA) append(b,"&"); else if(e->op==TOK_STAR) append(b,"*"); emit_expr(b,e->child); append(b,")"); break;
    default: break;
    }
}
static void emit_stmt(Buffer *b, Ast *n) {
    for (;n;n=n->next) switch(n->kind) {
    case AST_BLOCK: emit_stmt(b,n->child); break;
    case AST_EXPR_STMT: emit_expr(b,n->child); append(b,";\n"); break;
    case AST_ASSIGN:
        if(n->left && n->left->kind==AST_IDENT && n->left->is_pointer) {
            printbuf(b,"    %s %s = ",ctype(expr_type(n->right)),n->left->name); emit_expr(b,n->right); append(b,";\n"); break;
        }
        emit_expr(b,n->left); append(b," = "); emit_expr(b,n->right); append(b,";\n"); break;
    case AST_RETURN: append(b,"return "); emit_expr(b,n->child); append(b,";\n"); break;
    case AST_BREAK: append(b,"break;\n"); break;
    case AST_CONTINUE: append(b,"continue;\n"); break;
    case AST_IF: append(b,"if ("); emit_expr(b,n->cond); append(b,") {\n"); emit_stmt(b,n->body?n->body->child:NULL); append(b,"}\n"); if(n->else_branch){append(b,"else {\n");emit_stmt(b,n->else_branch->child);append(b,"}\n");} break;
    case AST_WHILE: append(b,"while ("); emit_expr(b,n->cond); append(b,") {\n"); emit_stmt(b,n->body?n->body->child:NULL); append(b,"}\n"); break;
    case AST_FOR: printbuf(b,"for (int %s = ",n->name);emit_expr(b,n->init);printbuf(b,"; %s < ",n->name);emit_expr(b,n->cond);printbuf(b,"; %s++ ) {\n",n->name);emit_stmt(b,n->body?n->body->child:NULL);append(b,"}\n");break;
    case AST_FUNC_DEF: printbuf(b,"/* function %s omitted in main-scope generator */\n",n->name?n->name:""); break;
    default: break;
    }
}

char *generate_c(Program *program) {
    Buffer b={0}; VarSet vars={0};
    if(!program || !program->root) return NULL;
    collect(program->root->child,&vars);
    append(&b,"#include <complex.h>\n#include <math.h>\n#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n\nint main(void) {\n");
    for(int i=0;i<vars.count;i++) {
        const char *t=ctype(vars.vars[i].type);
        printbuf(&b,"    %s %s = %s;\n",t,vars.vars[i].name,strchr(t,'*')?"NULL":"0");
    }
    if(vars.count) append(&b,"\n");
    emit_stmt(&b,program->root->child);
    append(&b,"    return 0;\n}\n");
    freevars(&vars);
    return b.data;
}
