#ifndef PEN_LAN_STRUCTS_H
#define PEN_LAN_STRUCTS_H

#include <stddef.h>
#include <stdlib.h>

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

#endif