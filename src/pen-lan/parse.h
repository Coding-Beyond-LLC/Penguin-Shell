#ifndef PEN_PARSE_H
#define PEN_PARSE_H

#include <stddef.h>
#include "./lan_structs.h"

typedef struct pen_ast_node pen_ast_node;

typedef enum {
    LINE,
    PIPELINE,
    PIPE_TAIL,
    COMMAND,
    ARG_LIST,
    EXEC,
    ARG,
    REDIRECT_LIST,
    REDIRECT,
    STMT,
    WORD_NODE,
    OP
} node_type;

struct pen_ast_node {
    pen_tok * tok;                 // borrowed: points into the tok_list, not owned by the node
    pen_ast_node ** nodes_children;
    size_t child_count;            // how many entries nodes_children holds (0 for leaves / ε)
    node_type node_type;
};

pen_ast_node * parse(pen_tok_list * tok_list);
void free_ast(pen_ast_node * node);

#endif