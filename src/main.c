#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "parser.h"
#include "codegen.h"

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "error: cannot open '%s'\n", path);
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *buf = malloc(size + 1);
    size_t got = fread(buf, 1, size, f);
    buf[got] = '\0';
    fclose(f);

    return buf;
}

static int write_file(const char *path, const char *content) {
    FILE *f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "error: cannot write '%s'\n", path);
        return 0;
    }
    fputs(content, f);
    fclose(f);
    return 1;
}

static int run_command(const char *cmd) {
    int rc = system(cmd);
    return WIFEXITED(rc) ? WEXITSTATUS(rc) : 1;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s [--tokens] [--emit-c] <file.108>\n", argv[0]);
        return 2;
    }

    const char *input_file = NULL;
    int show_tokens = 0;
    int emit_c_only = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--tokens")) {
            show_tokens = 1;
        } else if (!strcmp(argv[i], "--emit-c")) {
            emit_c_only = 1;
        } else {
            input_file = argv[i];
        }
    }

    if (!input_file) {
        fprintf(stderr, "error: no input file\n");
        return 2;
    }

    char *src = read_file(input_file);
    if (!src) {
        return 1;
    }

    /* Token dump mode */
    if (show_tokens) {
        Lexer lx;
        lexer_init(&lx, src);
        for (;;) {
            Token t = lexer_next(&lx);
            if (t.type == TOK_ERROR) {
                fprintf(stderr, "lex error at %d:%d: %s\n", t.line, t.col, t.msg);
                free(src);
                return 1;
            }
            printf("%4d:%-3d %-12s %.*s\n", t.line, t.col, token_name(t.type), (int)t.length, t.start);
            if (t.type == TOK_EOF) break;
        }
        free(src);
        return 0;
    }

    /* Parse */
    Program *prog = parse(src);
    if (!prog) {
        fprintf(stderr, "parse failed\n");
        free(src);
        return 1;
    }

    /* Generate C */
    char *c_code = generate_c(prog);

    if (emit_c_only) {
        printf("%s", c_code);
        free(c_code);
        free(src);
        ast_free(prog->root);
        free(prog);
        return 0;
    }

    /* Write .c file */
    char c_file[512];
    snprintf(c_file, sizeof(c_file), "/tmp/108_out.c");
    if (!write_file(c_file, c_code)) {
        free(c_code);
        free(src);
        ast_free(prog->root);
        free(prog);
        return 1;
    }

    /* Compile with gcc */
    char exe_file[512];
    snprintf(exe_file, sizeof(exe_file), "/tmp/108_out");

    char cc_cmd[1024];
    snprintf(cc_cmd, sizeof(cc_cmd), "gcc -std=c99 -o %s %s 2>&1", exe_file, c_file);

    fprintf(stderr, "[compile] ");
    int cc_rc = run_command(cc_cmd);
    if (cc_rc != 0) {
        fprintf(stderr, "C compiler failed\n");
        free(c_code);
        free(src);
        ast_free(prog->root);
        free(prog);
        return 1;
    }

    fprintf(stderr, "ok\n");

    /* Run executable */
    fprintf(stderr, "[run]\n");
    run_command(exe_file);

    /* Cleanup */
    unlink(c_file);
    unlink(exe_file);

    free(c_code);
    free(src);
    ast_free(prog->root);
    free(prog);

    return 0;
}
