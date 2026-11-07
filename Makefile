CC = gcc
CFLAGS = -std=c99 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -g
SRC = src/main.c src/lexer.c src/parser.c src/codegen.c
HDR = src/lexer.h src/parser.h src/codegen.h
BIN = 108c

all: $(BIN)

$(BIN): $(SRC) $(HDR)
	$(CC) $(CFLAGS) -o $(BIN) $(SRC)

clean:
	rm -f $(BIN) /tmp/108_out*

.PHONY: all clean
