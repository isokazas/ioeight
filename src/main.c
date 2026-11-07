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

static int compile_c_file(const char *c_file, const char *out_file) {
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return 1;
    }

    if (pid == 0) {
        char *argv[] = {
            "gcc",
            "-std=c99",
            "-O2",
            "-o",
            (char *)out_file,
            (char *)c_file,
            "-lm",
            NULL
        };
        execvp("gcc", argv);
        perror("execvp");
        _exit(127);
    }

    int status = 0;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s [--tokens] [--emit-c] -o <output> <file.108>\n", argv[0]);
        return 2;
    }

    const char *input_file = NULL;
    const char *output_file = NULL;
    int show_tokens = 0;
    int emit_c_only = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--tokens")) {
            show_tokens = 1;
        } else if (!strcmp(argv[i], "--emit-c")) {
            emit_c_only = 1;
        } else if (!strcmp(argv[i], "-o") || !strcmp(argv[i], "--output")) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: missing output path after %s\n", argv[i]);
                return 2;
            }
            output_file = argv[++i];
        } else {
            if (input_file) {
                fprintf(stderr, "error: multiple input files: '%s' and '%s'\n", input_file, argv[i]);
                return 2;
            }
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

    Program *prog = parse(src);
    if (!prog) {
        fprintf(stderr, "parse failed\n");
        free(src);
        return 1;
    }

    char *c_code = generate_c(prog);

    if (emit_c_only) {
        printf("%s", c_code);
        free(c_code);
        free(src);
        ast_free(prog->root);
        free(prog);
        return 0;
    }

    if (!output_file) {
        fprintf(stderr, "error: no output file specified; use -o <output>\n");
        free(c_code);
        free(src);
        ast_free(prog->root);
        free(prog);
        return 2;
    }

    char c_file[] = "/tmp/108_outXXXXXX.c";
    int fd = mkstemp(c_file);
    if (fd < 0) {
        perror("mkstemp");
        free(c_code);
        free(src);
        ast_free(prog->root);
        free(prog);
        return 1;
    }
    close(fd);

    if (!write_file(c_file, c_code)) {
        unlink(c_file);
        free(c_code);
        free(src);
        ast_free(prog->root);
        free(prog);
        return 1;
    }

    int cc_rc = compile_c_file(c_file, output_file);
    unlink(c_file);

    free(c_code);
    free(src);
    ast_free(prog->root);
    free(prog);

    if (cc_rc != 0) {
        fprintf(stderr, "C compiler failed\n");
        return 1;
    }

    return 0;
}
