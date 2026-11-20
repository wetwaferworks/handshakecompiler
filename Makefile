# Handshake Compiler (HC) — pure C11, zero mutexes.
CC      ?= gcc
CSTD     = -std=c11
WARN     = -Wall -Wextra -Werror
OPT     ?= -O2
DEFS     = -D_GNU_SOURCE
INC      = -Iinclude -Iruntime
CFLAGS  ?= $(CSTD) $(WARN) $(OPT) $(DEFS) $(INC)

SRC = src/lexer.c src/preproc.c src/parser.c src/symtab.c src/verify.c \
      src/lower.c src/backend_cc.c src/elf_audit.c src/main.c

HOST = build/host/hc
ASAN = build/asan/hc
CROSS = build/aarch64/hc

.PHONY: all asan cross snapshot clean rules

all: $(HOST) snapshot

$(HOST): $(SRC) $(wildcard include/*.h runtime/*.h)
	@mkdir -p build/host
	$(CC) $(CFLAGS) -o $@ $(SRC)

asan: $(SRC) $(wildcard include/*.h runtime/*.h)
	@mkdir -p build/asan
	$(CC) $(CSTD) $(WARN) $(DEFS) -fsanitize=address,undefined -fno-omit-frame-pointer -g $(INC) -o $(ASAN) $(SRC)

cross: $(SRC) $(wildcard include/*.h runtime/*.h)
	@mkdir -p build/aarch64
	aarch64-linux-gnu-gcc $(CSTD) $(WARN) $(OPT) $(DEFS) $(INC) -static -o $(CROSS) $(SRC)

rules: $(HOST)
	$(HOST) --dump-rules

# Keep-last-4 build snapshots with build/current pointing at the active one.
snapshot: $(HOST)
	@ts=$$(date +%Y%m%d-%H%M%S); \
	sha=$$(git rev-parse --short HEAD 2>/dev/null || echo nogit); \
	dir=build/history/$$ts-$$sha; \
	mkdir -p $$dir; \
	cp $(HOST) $$dir/hc; \
	ln -sfn $$(basename $$dir) build/history/latest; \
	ln -sfn history/$$ts-$$sha build/current; \
	cd build/history && ls -1dt */ 2>/dev/null | sed 's#/##' | grep -v '^latest$$' | tail -n +5 | xargs -r rm -rf; \
	echo "snapshot: build/current -> $$(readlink build/current)"

clean:
	rm -rf build
