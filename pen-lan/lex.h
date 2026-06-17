#ifndef PEN_LEX_H
#define PEN_LEX_H

#include <stdlib.h>
#include <stddef.h>

typedef enum {
    WORD,
    PIPE,
    REDIRECT_IN,
    REDIRECT_OUT,
    REDIRECT_APPEND,
    AND_IF,
    SEMI
} pen_tok_type;

typedef struct {
    char * text;
    pen_tok_type tok_type;
} pen_tok;

typedef struct {
    pen_tok * toks;
    char ** args;
    size_t n;
} pen_tok_list;

pen_tok_list * tokenize(char * input, size_t n);

#endif