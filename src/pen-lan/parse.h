#ifndef PEN_PARSE_H
#define PEN_PARSE_H

#include <stddef.h>
#include "./lan_structs.h"

typedef struct pen_ast_node pen_ast_node;

typedef enum {
    LINE,
    LIST,                // and_or (separator_op and_or)* -- ';' sequences, '&' backgrounds
    LIST_TAIL,
    AND_OR,               // pipeline ((AND_IF|OR_IF) pipeline)* -- '&&' / '||' short-circuit chain
    AND_OR_TAIL,
    PIPELINE,             // [Bang] pipe_sequence
    PIPE_SEQUENCE,        // command ('|' command)* -- what this codebase used to call PIPELINE
    PIPE_TAIL,
    COMMAND,
    ARG_LIST,
    EXEC,
    ARG,
    REDIRECT_LIST,
    REDIRECT,
    STMT,
    WORD_NODE,
    OP,

    // compound commands (PIPELINE's stage is one of these instead of a
    // PIPE_SEQUENCE when it starts with if/while/until/for -- see
    // generate_pipeline_node); a loop/if never participates in a pipe, so
    // these only ever appear as a whole pipeline's single stage
    IF_CLAUSE,      // [condition, then-body, else-part?] -- else-part is NULL, a LIST ('else'), or another IF_CLAUSE ('elif')
    WHILE_CLAUSE,   // [condition, body]
    UNTIL_CLAUSE,   // [condition, body]
    FOR_CLAUSE      // tok = loop variable; [wordlist, body]
} node_type;

struct pen_ast_node {
    pen_tok * tok;                 // borrowed: points into the tok_list, not owned by the node
    pen_ast_node ** nodes_children;
    size_t child_count;            // how many entries nodes_children holds (0 for leaves / ε)
    node_type node_type;

    // COMMAND nodes only: [tok_start, tok_end) indexes the contiguous run of
    // tokens this simple_command consumed from the tok_list, letting the
    // executor build a builtin-facing token slice scoped to just this
    // command instead of the whole (possibly multi-command) line
    size_t tok_start, tok_end;
};

pen_ast_node * parse(pen_tok_list * tok_list);
void free_ast(pen_ast_node * node);

#endif