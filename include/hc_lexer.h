/* SPDX-License-Identifier: MIT */
#ifndef HC_LEXER_H
#define HC_LEXER_H

#include <stddef.h>

typedef enum {
    HCT_EOF = 0,
    HCT_IDENT,
    HCT_NUMBER,
    HCT_STRING,   /* text includes surrounding quotes, escapes left raw */
    HCT_CHAR,
    HCT_PUNCT
} hct_kind;

typedef struct {
    hct_kind kind;
    char    *text;     /* NUL-terminated lexeme */
    int      line;     /* 1-based, mapped through #line / linemarkers */
    int      col;      /* 1-based column in the physical stream */
    int      system;   /* 1 = token originates in a system header */
    char    *file;     /* owning file name (interned, may be NULL) */
} hc_token;

typedef struct {
    hc_token *toks;
    size_t    count;
    size_t    cap;
} hc_token_list;

/*
 * Lex a preprocessed translation unit (GNU linemarkers understood). The
 * caller owns the returned list; free with hc_tokens_free.
 */
int  hc_lex_preprocessed(const char *buf, size_t len, hc_token_list *out);
void hc_tokens_free(hc_token_list *tl);

/* Decode a C string-literal lexeme (with quotes) into raw bytes. Returns the
 * decoded length and writes up to cap bytes into dst (NUL-terminated). */
size_t hc_decode_string_literal(const char *lexeme, char *dst, size_t cap);

#endif /* HC_LEXER_H */
