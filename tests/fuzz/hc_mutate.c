/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 HandshakeOS contributors
 *
 * hc_mutate - deterministic C-source mutator for fuzzing hc's lexer / preprocessor glue / parser /
 * verifier (tests/fuzz/fuzz-hc.sh drives it against an ASan+UBSan hc). Pure C11.
 * usage: hc_mutate IN.c OUT.c SEED */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t rs;
static uint64_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return rs; }

int main(int argc, char **argv)
{
    static const char *const tok[] = { "{", "}", "(", ")", "[", "]", "\"", "'", "/*", "*/", "//", "\\\n",
        "#define X(", "#if", "#endif", "__attribute__((", "asm(", "<<=",
        "...", "0x", "1e999", "L\"", "u8'", "\?\?/", "#include", "_Pragma(\"", ";", ",", "->", "::" };
    FILE *f;
    char *b, *o;
    long n;
    size_t i, k, on = 0, cap;
    if (argc < 4 || !(f = fopen(argv[1], "rb"))) return 2;
    rs = strtoull(argv[3], NULL, 10) * 0x9E3779B97F4A7C15ull | 1u;
    if (fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) <= 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return 2; }
    b = (char *)malloc((size_t)n);
    cap = (size_t)n * 2 + 70000;
    o = (char *)malloc(cap);
    if (!b || !o || fread(b, 1, (size_t)n, f) != (size_t)n) { fclose(f); return 2; }
    fclose(f);
    for (i = 0; i < (size_t)n && on + 64 < cap; i++) {
        uint64_t r = rnd() % 1000;
        if (r < 4) continue;                                    /* delete */
        if (r < 8) { const char *t = tok[rnd() % (sizeof tok / sizeof tok[0])]; size_t tl = strlen(t);
                     if (on + tl < cap) { memcpy(o + on, t, tl); on += tl; } }
        else if (r < 9) o[on++] = (char)(rnd() & 0xFF);          /* raw byte */
        else if (r < 10 && on + 4200 < cap) {                    /* long identifier / deep nesting */
            int deep = (int)(rnd() & 1);
            for (k = 0; k < 4096; k++) o[on++] = deep ? '(' : 'a';
        }
        if (on < cap) o[on++] = b[i];
    }
    if (!(f = fopen(argv[2], "wb"))) return 3;
    fwrite(o, 1, on, f);
    fclose(f);
    free(b); free(o);
    return 0;
}
