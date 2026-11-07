#include "parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

typedef struct { Token *tokens; int count; int pos; } Parser;
static Parser parser;
static char *pointer_vars[256];
static int pointer_var_count;

static Token peek(void) {
    if (parser.pos >= parser.count) return (Token){TOK_EOF,"",0,0,0,NULL};
    return parser.tokens[parser.pos];
}
static Token advance(void) { Token t=peek(); parser.pos++; return t; }
static int check(TokenType t) { return peek().type==t; }
static int match(TokenType t) { if(!check(t)) return 0; advance(); return 1; }
static Token consume(TokenType t,const char *msg) {
    if(!check(t)) fprintf(stderr,"Parse error at line %d: %s (expected %s, got %s)\n",peek().line,msg,token_name(t),token_name(peek().type));
    return advance();
}
static char *copy_token(Token t) { char *s=malloc(t.length+1); if(!s)return NULL; memcpy(s,t.start,t.length); s[t.length]='\0'; return s; }
static Ast *new_ast(AstKind k) { Ast *a=calloc(1,sizeof(*a)); if(a)a->kind=k; return a; }
static void skip_newlines(void) { while(match(TOK_NL)) {} }
static Ast *parse_expr(void); static Ast *parse_stmt(void); static Ast *parse_block(void);
static int is_type_token(TokenType t) {
    return t==TOK_T_INT||t==TOK_T_FLT||t==TOK_T_STR||t==TOK_T_BOOL||t==TOK_T_PNT||t==TOK_T_IMGINT||t==TOK_T_IMGFLT;
}
static int pointer_has(const char *name) {
    for(int i=0;i<pointer_var_count;i++) if(!strcmp(pointer_vars[i],name)) return 1;
    return 0;
}
static void pointer_add(const char *name) {
    if(!name||pointer_has(name)||pointer_var_count>=256) return;
    pointer_vars[pointer_var_count++]=copy_token((Token){TOK_IDENT,name,strlen(name),0,0,NULL});
}
static Ast *parse_primary(void) {
    skip_newlines();
    if(check(TOK_INT_LIT)) { Token t=advance(); Ast *n=new_ast(AST_INT); n->int_value=atoi(t.start); return n; }
    if(check(TOK_FLT_LIT)) { Token t=advance(); Ast *n=new_ast(AST_FLT); n->float_value=atof(t.start); return n; }
    if(check(TOK_STR_LIT)) { Token t=advance(); Ast *n=new_ast(AST_STR); n->str_value=copy_token(t); return n; }
    if(check(TOK_TRUE)||check(TOK_FALSE)) { int v=advance().type==TOK_TRUE; Ast *n=new_ast(AST_BOOL); n->bool_value=v; return n; }
    if(check(TOK_IDENT)||is_type_token(peek().type)||check(TOK_INPUT)) {
        Token t=advance(); Ast *n=new_ast(AST_IDENT); n->name=copy_token(t);
        if(n->name && pointer_has(n->name)) { Ast *d=new_ast(AST_UNARY_OP); d->op=TOK_STAR; d->child=n; return d; }
        return n;
    }
    if(match(TOK_LPAREN)) { Ast *e=parse_expr(); consume(TOK_RPAREN,"expected ')'"); return e; }
    if(match(TOK_LBRACKET)) {
        Ast *list=new_ast(AST_LIST); list->args=calloc(256,sizeof(Ast *));
        if(!check(TOK_RBRACKET)) do { if(list->arg_count<256) list->args[list->arg_count++]=parse_expr(); } while(match(TOK_COMMA));
        consume(TOK_RBRACKET,"expected ']'"); return list;
    }
    fprintf(stderr,"Parse error at line %d: unexpected token %s\n",peek().line,token_name(peek().type));
    if(!check(TOK_EOF)) advance();
    return NULL;
}
static Ast *parse_postfix(void) {
    Ast *e=parse_primary(); if(!e)return NULL;
    for(;;) {
        if(match(TOK_DOT)) {
            Token m=peek(); if(m.type==TOK_IDENT||m.type==TOK_INPUT||is_type_token(m.type)) advance(); else fprintf(stderr,"Parse error at line %d: expected member name\n",m.line);
            Ast *n=new_ast(AST_MEMBER_CALL); n->left=e; n->member=copy_token(m);
            if(match(TOK_LPAREN)) { n->args=calloc(16,sizeof(Ast *)); if(!check(TOK_RPAREN)) do { if(n->arg_count<16)n->args[n->arg_count++]=parse_expr(); } while(match(TOK_COMMA)); consume(TOK_RPAREN,"expected ')'"); }
            e=n;
        } else if(match(TOK_LPAREN)) {
            Ast *n=new_ast(AST_CALL); n->left=e; n->args=calloc(256,sizeof(Ast *));
            if(!check(TOK_RPAREN)) do { if(n->arg_count<256)n->args[n->arg_count++]=parse_expr(); } while(match(TOK_COMMA));
            consume(TOK_RPAREN,"expected ')'"); e=n;
        } else if(match(TOK_LBRACKET)) {
            Ast *n=new_ast(AST_BINARY_OP); n->op=TOK_LBRACKET; n->left=e; n->right=parse_expr(); consume(TOK_RBRACKET,"expected ']'"); e=n;
        } else break;
    }
    return e;
}
static Ast *parse_unary(void) {
    if(match(TOK_MINUS)||match(TOK_NOT)||match(TOK_TILDA)) {
        TokenType op=parser.tokens[parser.pos-1].type; Ast *n=new_ast(AST_UNARY_OP); n->op=op; n->child=parse_unary(); return n;
    }
    return parse_postfix();
}
static Ast *binary(Ast *l,TokenType op,Ast *r) { Ast *n=new_ast(AST_BINARY_OP); n->op=op;n->left=l;n->right=r;return n; }
static Ast *parse_power(void) { Ast *l=parse_unary(); if(match(TOK_CARET)) return binary(l,TOK_CARET,parse_unary()); return l; }
static Ast *parse_term(void) { Ast *l=parse_power(); while(check(TOK_STAR)||check(TOK_SLASH)||check(TOK_SLASH2)||check(TOK_PCT2)){TokenType o=advance().type;l=binary(l,o,parse_power());} return l; }
static Ast *parse_additive(void) { Ast *l=parse_term(); while(check(TOK_PLUS)||check(TOK_MINUS)){TokenType o=advance().type;l=binary(l,o,parse_term());} return l; }
static Ast *parse_relational(void) { Ast *l=parse_additive(); if(check(TOK_LT)||check(TOK_LE)||check(TOK_GT)||check(TOK_GE)){TokenType o=advance().type;return binary(l,o,parse_additive());} return l; }
static Ast *parse_equality(void) { Ast *l=parse_relational(); while(check(TOK_EQ)||check(TOK_NEQ)){TokenType o=advance().type;l=binary(l,o,parse_relational());} return l; }
static Ast *parse_and(void) { Ast *l=parse_equality(); while(match(TOK_AND))l=binary(l,TOK_AND,parse_equality()); return l; }
static Ast *parse_or(void) { Ast *l=parse_and(); while(match(TOK_OR))l=binary(l,TOK_OR,parse_and()); return l; }
static Ast *parse_expr(void) { return parse_or(); }
static Ast *parse_import(void) {
    Ast *i=new_ast(AST_IMPORT);
    if(match(TOK_ADDON)){i->name=copy_token(consume(TOK_IDENT,"expected module name"));if(match(TOK_AS))i->member=copy_token(consume(TOK_IDENT,"expected alias"));return i;}
    if(match(TOK_FROM)){i->name=copy_token(consume(TOK_IDENT,"expected module name"));consume(TOK_ADDEVERY,"expected 'addevery'");i->bool_value=1;return i;}
    free(i);return NULL;
}
static Ast *parse_if(void) {
    consume(TOK_IF,"expected 'if'"); Ast *i=new_ast(AST_IF); i->cond=parse_expr(); i->body=parse_block(); skip_newlines();
    if(match(TOK_ELIF)){ parser.pos--; i->else_branch=parse_if(); }
    else if(match(TOK_ELSE)) i->else_branch=parse_block();
    return i;
}
static Ast *parse_while(void) { consume(TOK_WHILE,"expected 'while'");Ast *n=new_ast(AST_WHILE);n->cond=parse_expr();n->body=parse_block();return n; }
static Ast *parse_for(void) {
    consume(TOK_FOR,"expected 'for'");Token v=consume(TOK_IDENT,"expected loop variable");consume(TOK_LPAREN,"expected '('");Ast *n=new_ast(AST_FOR);n->name=copy_token(v);n->init=parse_expr();consume(TOK_COMMA,"expected ','");n->cond=parse_expr();if(match(TOK_COMMA))n->update=parse_expr();consume(TOK_RPAREN,"expected ')'");n->body=parse_block();return n;
}
static Ast *parse_funcdef(void) {
    consume(TOK_DEF,"expected 'def'");Ast *f=new_ast(AST_FUNC_DEF);f->name=copy_token(consume(TOK_IDENT,"expected function name"));consume(TOK_LPAREN,"expected '('");f->params=calloc(256,sizeof(Ast *));
    if(!check(TOK_RPAREN)) do {Token pn=consume(TOK_IDENT,"expected param name");consume(TOK_COLON,"expected ':'");Token pt=peek();if(is_type_token(pt.type))advance();else fprintf(stderr,"Parse error at line %d: expected type\n",pt.line);Ast *p=new_ast(AST_IDENT);p->name=copy_token(pn);p->return_type=copy_token(pt);if(f->param_count<256)f->params[f->param_count++]=p;} while(match(TOK_COMMA));
    consume(TOK_RPAREN,"expected ')'");if(match(TOK_ARROW))f->return_type=copy_token(advance());f->body=parse_block();return f;
}
static Ast *parse_stmt(void) {
    skip_newlines();
    if(check(TOK_IF))return parse_if();if(check(TOK_WHILE))return parse_while();if(check(TOK_FOR))return parse_for();if(check(TOK_DEF))return parse_funcdef();
    if(match(TOK_RETURN)){Ast *n=new_ast(AST_RETURN);if(!check(TOK_NL)&&!check(TOK_RBRACE)&&!check(TOK_EOF))n->child=parse_expr();return n;}
    if(match(TOK_BREAK))return new_ast(AST_BREAK);if(match(TOK_CONTINUE))return new_ast(AST_CONTINUE);
    if(match(TOK_PTR)) {Token v=consume(TOK_IDENT,"expected pointer name");consume(TOK_ASSIGN,"expected '='");Ast *n=new_ast(AST_ASSIGN);n->left=new_ast(AST_IDENT);n->left->name=copy_token(v);n->left->is_pointer=1;pointer_add(n->left->name);n->right=parse_expr();return n;}
    Ast *e=parse_expr();if(!e)return NULL;
    if(match(TOK_ASSIGN)){Ast *n=new_ast(AST_ASSIGN);n->left=e;n->right=parse_expr();return n;}
    if(match(TOK_PLUS_EQ)||match(TOK_MINUS_EQ)||match(TOK_STAR_EQ)||match(TOK_SLASH_EQ)||match(TOK_SLASH2_EQ)||match(TOK_PCT2_EQ)||match(TOK_CARET_EQ)){Ast *n=new_ast(AST_ASSIGN);n->op=parser.tokens[parser.pos-1].type;n->left=e;n->right=parse_expr();return n;}
    Ast *n=new_ast(AST_EXPR_STMT);n->child=e;return n;
}
static Ast *parse_block(void) {
    consume(TOK_LBRACE,"expected '{'");Ast *b=new_ast(AST_BLOCK),*tail=NULL;skip_newlines();
    while(!check(TOK_RBRACE)&&!check(TOK_EOF)){int before=parser.pos;Ast *s=parse_stmt();if(s){if(!tail)b->child=s;else tail->next=s;tail=s;}if(parser.pos==before)advance();skip_newlines();}
    consume(TOK_RBRACE,"expected '}'");return b;
}
Program *parse(const char *src) {
    Lexer lx;lexer_init(&lx,src);Token tokens[8192];int count=0;pointer_var_count=0;
    for(;;){Token t=lexer_next(&lx);if(t.type==TOK_ERROR){fprintf(stderr,"Lex error at line %d col %d: %s\n",t.line,t.col,t.msg);return NULL;}if(count>=8191)break;tokens[count++]=t;if(t.type==TOK_EOF)break;}
    parser.tokens=tokens;parser.count=count;parser.pos=0;Program *p=calloc(1,sizeof(*p));Ast *root=new_ast(AST_BLOCK),*tail=NULL;skip_newlines();
    while(!check(TOK_EOF)){Ast *s=(check(TOK_ADDON)||check(TOK_FROM))?parse_import():parse_stmt();if(s){if(!tail)root->child=s;else tail->next=s;tail=s;}skip_newlines();}
    p->root=root;return p;
}
void ast_free(Ast *n) {
    if(!n)return;free(n->str_value);free(n->name);free(n->member);free(n->return_type);
    if(n->params){for(int i=0;i<n->param_count;i++)ast_free(n->params[i]);free(n->params);}
    if(n->args){for(int i=0;i<n->arg_count;i++)ast_free(n->args[i]);free(n->args);}
    ast_free(n->child);ast_free(n->left);ast_free(n->right);ast_free(n->cond);ast_free(n->body);ast_free(n->else_branch);ast_free(n->init);ast_free(n->update);ast_free(n->next);free(n);
}
