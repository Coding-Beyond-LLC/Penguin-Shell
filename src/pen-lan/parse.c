#include "./parse.h"
#include "lan_structs.h"
#include <stdlib.h>

// ---- parser state -------------------------------------------------------
// The cursor is the one piece of mutable state shared across every recursive
// call. `pos` always indexes the next *unconsumed* token, so a production
// "consumes" input simply by advancing it.
typedef struct {
    pen_tok_list * toks;
    size_t pos;
    int error;
} parser;

static int at_end(parser * p) {
    return p->pos >= p->toks->n;
}

// true only when a current token exists AND matches t (false at end of input)
static int check(parser * p, pen_tok_type t) {
    return !at_end(p) && p->toks->toks[p->pos].tok_type == t;
}

// hand back the current token and step past it
static pen_tok * advance(parser * p) {
    return &p->toks->toks[p->pos++];
}

// ---- node construction --------------------------------------------------
static pen_ast_node * new_node(node_type type) {
    pen_ast_node * node = malloc(sizeof(pen_ast_node));
    node->tok = NULL;
    node->nodes_children = NULL;
    node->child_count = 0;
    node->node_type = type;
    return node;
}

// children is an array of *pointers*, so the element size is sizeof(pen_ast_node *)
static void alloc_children(pen_ast_node * node, size_t count) {
    node->nodes_children = malloc(sizeof(pen_ast_node *) * count);
    node->child_count = count;
}

// ---- grammar productions ------------------------------------------------
// forward declarations: the productions are mutually recursive
static pen_ast_node * generate_exec_node(parser * p);
static pen_ast_node * generate_arg_node(parser * p);
static pen_ast_node * generate_arg_list_node(parser * p);
static pen_ast_node * generate_redirect_node(parser * p);
static pen_ast_node * generate_redirect_list_node(parser * p);
static pen_ast_node * generate_command_node(parser * p);
static pen_ast_node * generate_pipeline_tail_node(parser * p);
static pen_ast_node * generate_pipeline_node(parser * p);
static pen_ast_node * generate_line_node(parser * p);

// <exec> ::= WORD
static pen_ast_node * generate_exec_node(parser * p) {
    pen_ast_node * node = new_node(EXEC);
    if (!check(p, WORD)) {        // a command must start with a word
        p->error = 1;
        return node;              // tok stays NULL; parse() bails on p->error
    }
    node->tok = advance(p);       // borrow the WORD token (owned by the tok_list)
    return node;
}

// <arg> ::= WORD
static pen_ast_node * generate_arg_node(parser * p) {
    pen_ast_node * node = new_node(ARG);
    node->tok = advance(p);       // only reached when the caller saw a WORD
    return node;
}

// <arg_list> ::= <arg> <arg_list> | ε
static pen_ast_node * generate_arg_list_node(parser * p) {
    pen_ast_node * node = new_node(ARG_LIST);
    if (check(p, WORD)) {
        alloc_children(node, 2);
        node->nodes_children[0] = generate_arg_node(p);
        node->nodes_children[1] = generate_arg_list_node(p);
    }
    return node;                  // ε: child_count stays 0, children stays NULL
}

// <redirect> ::= (REDIRECT_IN | REDIRECT_OUT | REDIRECT_APPEND) WORD
// node->tok holds the operator; nodes_children[0] is an ARG leaf with the filename
static pen_ast_node * generate_redirect_node(parser * p) {
    pen_ast_node * node = new_node(REDIRECT);
    node->tok = advance(p);             // consume the redirect operator
    if (!check(p, WORD)) {
        p->error = 1;
        return node;                    // missing filename — parse() will bail
    }
    alloc_children(node, 1);
    pen_ast_node * file_node = new_node(ARG);
    file_node->tok = advance(p);        // borrow the filename WORD
    node->nodes_children[0] = file_node;
    return node;
}

// <redirect_list> ::= <redirect> <redirect_list> | ε
static pen_ast_node * generate_redirect_list_node(parser * p) {
    pen_ast_node * node = new_node(REDIRECT_LIST);
    if (check(p, REDIRECT_IN) || check(p, REDIRECT_OUT) || check(p, REDIRECT_APPEND)) {
        alloc_children(node, 2);
        node->nodes_children[0] = generate_redirect_node(p);
        node->nodes_children[1] = generate_redirect_list_node(p);
    }
    return node;                        // ε: child_count stays 0
}

// <command> ::= <exec> <arg_list> <redirect_list>
static pen_ast_node * generate_command_node(parser * p) {
    pen_ast_node * node = new_node(COMMAND);
    alloc_children(node, 3);
    node->nodes_children[0] = generate_exec_node(p);
    node->nodes_children[1] = generate_arg_list_node(p);
    node->nodes_children[2] = generate_redirect_list_node(p);
    return node;
}

// <pipe_tail> ::= PIPE <command> <pipe_tail> | ε
static pen_ast_node * generate_pipeline_tail_node(parser * p) {
    pen_ast_node * node = new_node(PIPE_TAIL);
    if (check(p, PIPE)) {
        advance(p);               // consume the '|'
        alloc_children(node, 2);
        node->nodes_children[0] = generate_command_node(p);
        node->nodes_children[1] = generate_pipeline_tail_node(p);
    }
    return node;                  // ε
}

// <pipeline> ::= <command> <pipe_tail>
static pen_ast_node * generate_pipeline_node(parser * p) {
    pen_ast_node * node = new_node(PIPELINE);
    alloc_children(node, 2);
    node->nodes_children[0] = generate_command_node(p);
    node->nodes_children[1] = generate_pipeline_tail_node(p);
    return node;
}

// <line> ::= <pipeline> | ε
static pen_ast_node * generate_line_node(parser * p) {
    pen_ast_node * node = new_node(LINE);
    if (!at_end(p)) {
        alloc_children(node, 1);
        node->nodes_children[0] = generate_pipeline_node(p);
    }
    return node;                  // empty line: 0 children, not an error
}

// ---- public API ---------------------------------------------------------
// Frees the tree. node->tok is borrowed from the tok_list (freed by
// free_tokens), so it is deliberately NOT freed here.
void free_ast(pen_ast_node * node) {
    if (node == NULL) return;
    for (size_t i = 0; i < node->child_count; i++) {
        free_ast(node->nodes_children[i]);
    }
    free(node->nodes_children);   // free(NULL) is a no-op for leaves / ε nodes
    free(node);
}

// Returns the LINE root, or NULL on a syntax error (a missing command operand
// or a token the grammar can't yet consume). An empty line is a valid parse:
// a LINE node with zero children.
pen_ast_node * parse(pen_tok_list * tok_list) {
    parser p = { .toks = tok_list, .pos = 0, .error = 0 };

    pen_ast_node * root = generate_line_node(&p);

    // p.error  -> a production failed (e.g. "| ls", "ls |")
    // !at_end  -> tokens left over the grammar didn't consume (e.g. a stray ;)
    if (p.error || !at_end(&p)) {
        free_ast(root);
        return NULL;
    }

    return root;
}