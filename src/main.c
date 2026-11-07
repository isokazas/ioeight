#define _POSIX_C_SOURCE 200809L

#include <errno.h>
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

    if (fseek(f, 0, SEEK_END) != 0) {
        fprintf(stderr, "error: cannot seek '%s'\n", path);
        fclose(f);
        return NULL;
    }
    long size = ftell(f);
    if (size < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fprintf(stderr, "error: cannot read '%s'\n", path);
        fclose(f);
        return NULL;
    }

    char *buf = malloc((size_t)size + 1);
    if (!buf) {
        fprintf(stderr, "error: out of memory\n");
        fclose(f);
        return NULL;
    }
    size_t got = fread(buf, 1, (size_t)size, f);
    if (ferror(f)) {
        fprintf(stderr, "error: failed reading '%s'\n", path);
        free(buf);
        fclose(f);
        return NULL;
    }
    buf[got] = '\0';
    fclose(f);
    return buf;
}

static int compile_c_file(const char *c_file, const char *out_file) {
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return 1;
    }

    if (pid == 0) {
        char *const child_argv[] = {
            "gcc", "-std=gnu99", "-O2", "-o", (char *)out_file,
            (char *)c_file, "-lm", NULL
        };
        execvp("gcc", child_argv);
        perror("execvp gcc");
        _exit(127);
    }

    int status;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno == EINTR) continue;
        perror("waitpid");
        return 1;
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}

static void cleanup(Program *prog, char *src, char *c_code) {
    free(c_code);
    free(src);
    if (prog) {
        ast_free(prog->root);
        free(prog);
    }
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s [--tokens | --emit-c] <file.108> [-o <output>]\n", argv[0]);
        return 2;
    }

    const char *input_file = NULL;
    const char *output_file = NULL;
    int show_tokens = 0;
    int emit_c_only = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--tokens") == 0) {
            show_tokens = 1;
        } else if (strcmp(argv[i], "--emit-c") == 0) {
            emit_c_only = 1;
        } else if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) {
            if (i + 1 >= argc || argv[i + 1][0] == '\0') {
                fprintf(stderr, "error: missing output path after %s\n", argv[i]);
                return 2;
            }
            output_file = argv[++i];
        } else if (argv[i][0] == '-' && strcmp(argv[i], "-") != 0) {
            fprintf(stderr, "error: unknown option '%s'\n", argv[i]);
            return 2;
        } else if (!input_file) {
            input_file = argv[i];
        } else {
            fprintf(stderr, "error: multiple input files: '%s' and '%s'\n", input_file, argv[i]);
            return 2;
        }
    }

    if (!input_file) {
        fprintf(stderr, "error: no input file\n");
        return 2;
    }
    if (show_tokens && emit_c_only) {
        fprintf(stderr, "error: --tokens and --emit-c cannot be used together\n");
        return 2;
    }
    if ((show_tokens || emit_c_only) && output_file) {
        fprintf(stderr, "error: -o cannot be used with --tokens or --emit-c\n");
        return 2;
    }

    char *src = read_file(input_file);
    if (!src) return 1;

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
    if (!c_code) {
        fprintf(stderr, "error: failed to generate C code\n");
        cleanup(prog, src, NULL);
        return 1;
    }

    if (emit_c_only) {
        fputs(c_code, stdout);
        cleanup(prog, src, c_code);
        return 0;
    }

    if (!output_file) {
        fprintf(stderr, "error: no output file specified; use -o <output>\n");
        cleanup(prog, src, c_code);
        return 2;
    }

    /* mkstemp requires six X characters at the end of the template. */
    char c_file[] = "/tmp/ioeight-XXXXXX";
    int fd = mkstemp(c_file);
    if (fd < 0) {
        perror("mkstemp");
        cleanup(prog, src, c_code);
        return 1;
    }

    FILE *temp = fdopen(fd, "w");
    if (!temp) {
        perror("fdopen");
        close(fd);
        unlink(c_file);
        cleanup(prog, src, c_code);
        return 1;
    }
    int wrote_ok = fputs(c_code, temp) >= 0;
    if (fclose(temp) != 0) wrote_ok = 0;
    if (!wrote_ok) {
        fprintf(stderr, "error: failed writing temporary C source\n");
        unlink(c_file);
        cleanup(prog, src, c_code);
        return 1;
    }

    int cc_rc = compile_c_file(c_file, output_file);
    unlink(c_file);
    cleanup(prog, src, c_code);

    if (cc_rc != 0) {
        fprintf(stderr, "error: C compiler failed (exit status %d)\n", cc_rc);
        return cc_rc;
    }
    return 0;
}
