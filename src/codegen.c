#include "codegen.h"

#include <complex.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { char *s; size_t len, cap; } Buffer;
typedef struct { char *name; const char *type; } Var;
typedef struct { Var v[256]; int n; } Vars;

static void reserve(Buffer *b, size_t extra) {
    if (b->len + extra + 1 <= b->cap) return;
    size_t c = b->cap ? b->cap : 1024;
    while (c < b->len + extra + 1) c *= 2;
    char *p = realloc(b->s, c);
    if (!p) return;
    b->s = p; b->cap = c;
}
static void add(Buffer *b, const char *s) {
    if (!s) return;
    size_t n = strlen(s); reserve(b, n);
    if (!b->s || b->len + n + 1 > b->cap) return;
    memcpy(b->s + b->len, s, n + 1); b->len += n;
}
static void fmt(Buffer *b, const char *f, ...) {
    va_list ap; va_start(ap, f); int n = vsnprintf(NULL, 0, f, ap); va_end(ap);
    if (n < 0) return; reserve(b, (size_t)n);
    if (!b->s || b->len + (size_t)n + 1 > b->cap) return;
    va_start(ap, f); vsnprintf(b->s + b->len, b->cap - b->len, f, ap); va_end(ap); b->len += (size_t)n;
}
static const char *ct(const char *t) {
    if (!t) return "int";
    if (!strcmp(t,"flt") || !strcmp(t,"double")) return "double";
    if (!strcmp(t,"str") || !strcmp(t,"char *")) return "char *";
    if (!strcmp(t,"pnt") || !strcmp(t,"void *")) return "void *";
    if (!strcmp(t,"FILE *")) return "FILE *";
    if (!strcmp(t,"double complex") || !strcmp(t,"imgint") || !strcmp(t,"imgflt")) return "double complex";
    if (!strcmp(t,"int *") || !strcmp(t,"double *") || !strcmp(t,"char **") || !strcmp(t,"void **") || !strcmp(t,"double complex *")) return t;
    return "int";
}
static const char *infer(Ast *e) {
    if (!e) return "int";
    switch(e->kind) {
    case AST_FLT: return "double"; case AST_STR: return "char *"; case AST_LIST: return "int *";
    case AST_INT: case AST_BOOL: return "int";
    case AST_CALL:
        if(e->left && e->left->kind==AST_IDENT && e->left->name) {
            if(!strcmp(e->left->name,"imgint") || !strcmp(e->left->name,"imgflt")) return "double complex";
            if(!strcmp(e->left->name,"openfile")) return "FILE *";
        } break;
    case AST_UNARY_OP:
        if(e->op==TOK_TILDA) {
            const char *t=infer(e->child);
            if(!strcmp(t,"double"))return "double *";
            if(!strcmp(t,"double complex"))return "double complex *";
            if(!strcmp(t,"char *"))return "char **";
            if(!strcmp(t,"void *"))return "void **";
            return "int *";
        }
        break;
    default: break;
    }
    return "int";
}
static int find(Vars *v,const char *name) { for(int i=0;i<v->n;i++) if(!strcmp(v->v[i].name,name)) return i; return -1; }
static void var_add(Vars *v,const char *name,const char *type) {
    if(!name||!*name)return; int i=find(v,name);
    if(i>=0){ if(v->v[i].type && !strcmp(v->v[i].type,"int") && strcmp(type,"int"))v->v[i].type=type; return; }
    if(v->n>=256)return; char *p=malloc(strlen(name)+1); if(!p)return; strcpy(p,name); v->v[v->n++]=(Var){p,type};
}
static void var_free(Vars *v){for(int i=0;i<v->n;i++)free(v->v[i].name);}
static int builtin(const char *s) {
    static const char *a[]={"print","sin","cos","tan","asin","acos","atan","exp","log","log10","sqrt","pow","fabs","openfile","closefile","fprintf","fread","fwrite","imgint","imgflt","int","flt","str","bool","pnt"};
    for(size_t i=0;i<sizeof(a)/sizeof(a[0]);i++)if(!strcmp(s,a[i]))return 1; return 0;
}
static void collect_expr(Ast *e,Vars *v) {
    if(!e)return; if(e->kind==AST_IDENT&&e->name&&!builtin(e->name))var_add(v,e->name,"int");
    collect_expr(e->left,v);collect_expr(e->right,v);collect_expr(e->child,v);
    for(int i=0;i<e->arg_count;i++)collect_expr(e->args[i],v);
}
static void collect(Ast *n,Vars *v) {
    for(;n;n=n->next)switch(n->kind){
    case AST_ASSIGN:
        if(n->left&&n->left->kind==AST_IDENT)var_add(v,n->left->name,infer(n->right));
        collect_expr(n->left,v);collect_expr(n->right,v);break;
    case AST_IF: collect_expr(n->cond,v);if(n->body)collect(n->body->child,v);if(n->else_branch){if(n->else_branch->kind==AST_BLOCK)collect(n->else_branch->child,v);else collect(n->else_branch,v);}break;
    case AST_WHILE:collect_expr(n->cond,v);if(n->body)collect(n->body->child,v);break;
    case AST_FOR:var_add(v,n->name,"int");collect_expr(n->init,v);collect_expr(n->cond,v);collect_expr(n->update,v);if(n->body)collect(n->body->child,v);break;
    case AST_BLOCK:collect(n->child,v);break; case AST_EXPR_STMT:collect_expr(n->child,v);break;case AST_RETURN:collect_expr(n->child,v);break;default:break;
    }
}
static void expr(Buffer *,Ast *); static void stmts(Buffer *,Ast *);
static void args(Buffer *b,Ast *e){for(int i=0;i<e->arg_count;i++){if(i)add(b,", ");expr(b,e->args[i]);}}
static void expr(Buffer *b,Ast *e) {
    if(!e)return;
    switch(e->kind){
    case AST_INT:fmt(b,"%d",e->int_value);break;case AST_FLT:fmt(b,"%.17g",e->float_value);break;
    case AST_STR:add(b,e->str_value?e->str_value:"\"\"");break;case AST_BOOL:add(b,e->bool_value?"1":"0");break;
    case AST_IDENT:add(b,e->name);break;
    case AST_LIST:add(b,"(int[]){ ");args(b,e);add(b," }");break;
    case AST_CALL:{const char *name=e->left&&e->left->kind==AST_IDENT?e->left->name:NULL;
        if(name&&!strcmp(name,"print")){Ast *a=e->arg_count?e->args[0]:NULL;
            if(a&&a->kind==AST_STR){add(b,"printf(\"%s\\n\", ");expr(b,a);add(b,")");}
            else if(a&&a->kind==AST_CALL&&a->left&&a->left->kind==AST_IDENT&&(!strcmp(a->left->name,"imgint")||!strcmp(a->left->name,"imgflt"))){add(b,"printf(\"(%g%+gi)\\n\", creal(");expr(b,a);add(b,"), cimag(");expr(b,a);add(b,"))");}
            else if(a&&!strcmp(infer(a),"double")){add(b,"printf(\"%g\\n\", (double)(");expr(b,a);add(b,"))");}
            else {add(b,"printf(\"%d\\n\", (int)(");if(a)expr(b,a);else add(b,"0");add(b,"))");} break;}
        if(name&&(!strcmp(name,"imgint")||!strcmp(name,"imgflt"))){add(b,"((double complex)((double)(");if(e->arg_count)expr(b,e->args[0]);add(b,") + (double)(");if(e->arg_count>1)expr(b,e->args[1]);add(b,") * I))");break;}
        if(name&&!strcmp(name,"openfile"))add(b,"fopen");else if(name&&!strcmp(name,"closefile"))add(b,"fclose");else if(name)add(b,name);else expr(b,e->left);
        add(b,"(");args(b,e);add(b,")");break;}
    case AST_MEMBER_CALL:
        if(e->left&&e->left->kind==AST_IDENT&&e->member&&!strcmp(e->member,"input")){
            if(!strcmp(e->left->name,"int")){add(b,"({ int _v=0; printf(");if(e->arg_count)expr(b,e->args[0]);else add(b,"\"\"");add(b,"); scanf(\"%d\", &_v); _v; })");}
            else if(!strcmp(e->left->name,"flt")){add(b,"({ double _v=0; printf(");if(e->arg_count)expr(b,e->args[0]);else add(b,"\"\"");add(b,"); scanf(\"%lf\", &_v); _v; })");}else add(b,"0");
        }else{expr(b,e->left);add(b,"_");add(b,e->member);}break;
    case AST_BINARY_OP:
        if(e->op==TOK_LBRACKET){expr(b,e->left);add(b,"[");expr(b,e->right);add(b,"]");break;}
        if(e->op==TOK_CARET){add(b,"pow(");expr(b,e->left);add(b,", ");expr(b,e->right);add(b,")");break;}
        add(b,"(");expr(b,e->left);add(b," ");
        switch(e->op){case TOK_PLUS:add(b,"+");break;case TOK_MINUS:add(b,"-");break;case TOK_STAR:add(b,"*");break;case TOK_SLASH:case TOK_SLASH2:add(b,"/");break;case TOK_PCT2:add(b,"%");break;case TOK_EQ:add(b,"==");break;case TOK_NEQ:add(b,"!=");break;case TOK_LT:add(b,"<");break;case TOK_LE:add(b,"<=");break;case TOK_GT:add(b,">");break;case TOK_GE:add(b,">=");break;case TOK_AND:add(b,"&&");break;case TOK_OR:add(b,"||");break;default:add(b,"?");break;}
        add(b," ");expr(b,e->right);add(b,")");break;
    case AST_UNARY_OP:add(b,"(");if(e->op==TOK_MINUS)add(b,"-");else if(e->op==TOK_NOT)add(b,"!");else if(e->op==TOK_TILDA)add(b,"&");else if(e->op==TOK_STAR)add(b,"*");expr(b,e->child);add(b,")");break;
    default:break;
    }
}
static void stmts(Buffer *b,Ast *n){
    for(;n;n=n->next)switch(n->kind){
    case AST_BLOCK:stmts(b,n->child);break;
    case AST_EXPR_STMT:add(b,"    ");expr(b,n->child);add(b,";\n");break;
    case AST_ASSIGN:add(b,"    ");expr(b,n->left);add(b," = ");expr(b,n->right);add(b,";\n");break;
    case AST_RETURN:add(b,"    return");if(n->child){add(b," ");expr(b,n->child);}add(b,";\n");break;
    case AST_BREAK:add(b,"    break;\n");break;case AST_CONTINUE:add(b,"    continue;\n");break;
    case AST_IF:add(b,"    if (");expr(b,n->cond);add(b,") {\n");if(n->body)stmts(b,n->body->child);add(b,"    }\n");if(n->else_branch){add(b,"    else {\n");if(n->else_branch->kind==AST_BLOCK)stmts(b,n->else_branch->child);else stmts(b,n->else_branch);add(b,"    }\n");}break;
    case AST_WHILE:add(b,"    while (");expr(b,n->cond);add(b,") {\n");if(n->body)stmts(b,n->body->child);add(b,"    }\n");break;
    case AST_FOR:fmt(b,"    for (int %s = ",n->name);expr(b,n->init);fmt(b,"; %s < ",n->name);expr(b,n->cond);fmt(b,"; %s++ ) {\n",n->name);if(n->body)stmts(b,n->body->child);add(b,"    }\n");break;
    default:break;
    }
}
static void function(Buffer *b,Ast *f){
    fmt(b,"static %s %s(",ct(f->return_type),f->name?f->name:"fn");
    for(int i=0;i<f->param_count;i++){if(i)add(b,", ");fmt(b,"%s %s",ct(f->params[i]->return_type),f->params[i]->name);}
    if(!f->param_count)add(b,"void");add(b,") {\n");Vars v={0};if(f->body)collect(f->body->child,&v);
    for(int i=0;i<f->param_count;i++){int p=find(&v,f->params[i]->name);if(p>=0){free(v.v[p].name);for(int j=p;j<v.n-1;j++)v.v[j]=v.v[j+1];v.n--;}}
    for(int i=0;i<v.n;i++){const char *t=ct(v.v[i].type);fmt(b,"    %s %s = %s;\n",t,v.v[i].name,strchr(t,'*')?"NULL":"0");}
    if(v.n)add(b,"\n");if(f->body)stmts(b,f->body->child);add(b,"}\n\n");var_free(&v);
}
char *generate_c(Program *program){
    if(!program||!program->root)return NULL;Buffer b={0};
    add(&b,"#include <complex.h>\n#include <math.h>\n#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n\n");
    for(Ast *n=program->root->child;n;n=n->next)if(n->kind==AST_FUNC_DEF)function(&b,n);
    add(&b,"int main(void) {\n");Vars vars={0};
    /* Walk the statement chain once: each collector/emitter already visits next siblings. */
    collect(program->root->child,&vars);
    for(int i=0;i<vars.n;i++){const char *t=ct(vars.v[i].type);fmt(&b,"    %s %s = %s;\n",t,vars.v[i].name,strchr(t,'*')?"NULL":"0");}
    if(vars.n)add(&b,"\n");stmts(&b,program->root->child);add(&b,"    return 0;\n}\n");var_free(&vars);return b.s;
}
