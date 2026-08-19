#ifndef PEN_LAN_STRUCTS_H
#define PEN_LAN_STRUCTS_H

#include <stddef.h>
#include <stdlib.h>

typedef enum {
    WORD,
    PIPE,
    REDIRECT_IN,
    REDIRECT_OUT,
    REDIRECT_APPEND,     // '>>' (POSIX DGREAT)
    AND_IF,              // '&&'
    OR_IF,                // '||'
    DSEMI,                // ';;'
    DLESS,                // '<<'
    DLESSDASH,             // '<<-'
    LESSAND,               // '<&'
    GREATAND,              // '>&'
    LESSGREAT,             // '<>'
    CLOBBER,               // '>|'
    SEMI,
    AMP,                   // '&' (separator_op, backgrounds a list item)
    ENV_VAR,
    EQ,                     // this shell's WORD EQ WORD assignment grammar, not POSIX

    // reserved words (see POSIX XCU 2.4) -- only ever assigned to a WORD
    // token that occurs in "command name" position (Shell Grammar Rules,
    // rule 1); elsewhere the same text stays a plain WORD
    BANG,
    LBRACE,
    RBRACE,
    CASE,
    DO,
    DONE,
    ELIF,
    ELSE,
    ESAC,
    FI,
    FOR,
    IF,
    IN,
    THEN,
    UNTIL,
    WHILE
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