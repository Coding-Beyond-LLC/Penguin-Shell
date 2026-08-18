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
    node->tok_start = 0;
    node->tok_end = 0;
    return node;
}

// children is an array of *pointers*, so the element size is sizeof(pen_ast_node *)
static void alloc_children(pen_ast_node * node, size_t count) {
    node->nodes_children = malloc(sizeof(pen_ast_node *) * count);
    node->child_count = count;
}

// ---- grammar productions ------------------------------------------------
// This is the "lists / and_or / pipeline / simple_command" slice of the
// POSIX Shell Grammar (XCU 2.10.2): reserved words are lexed and a leading
// Bang negates a pipeline, but compound commands (if/for/while/until/case,
// subshells, brace groups, function definitions) are not yet implemented --
// a reserved word token appearing where a WORD is expected is a syntax
// error (generate_exec_node's check(p, WORD) fails), same as any other
// token the grammar can't consume.
//
// forward declarations: the productions are mutually recursive
static pen_ast_node * generate_exec_node(parser * p);
static pen_ast_node * generate_arg_node(parser * p);
static pen_ast_node * generate_arg_list_node(parser * p);
static pen_ast_node * generate_redirect_node(parser * p);
static pen_ast_node * generate_redirect_list_node(parser * p);
static pen_ast_node * generate_command_node(parser * p);
static pen_ast_node * generate_pipe_tail_node(parser * p);
static pen_ast_node * generate_pipe_sequence_node(parser * p);
static pen_ast_node * generate_pipeline_node(parser * p);
static pen_ast_node * generate_and_or_tail_node(parser * p);
static pen_ast_node * generate_and_or_node(parser * p);
static pen_ast_node * generate_list_tail_node(parser * p);
static pen_ast_node * generate_list_node(parser * p);
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

// <stmt> ::= WORD EQ WORD
static pen_ast_node * generate_stmt_node(parser * p) {
    pen_ast_node * node = new_node(STMT);
    alloc_children(node, 3);
    node->nodes_children[0] = NULL;
    node->nodes_children[1] = NULL;
    node->nodes_children[2] = NULL;

    pen_ast_node * var_node = new_node(WORD_NODE);
    var_node->tok = advance(p);
    node->nodes_children[0] = var_node;

    if (!check(p, EQ)) {
        p->error = 1;
        return node;
    }
    pen_ast_node * eq_node = new_node(OP);
    eq_node->tok = advance(p);
    node->nodes_children[1] = eq_node;

    if (!check(p, WORD)) {
        p->error = 1;
        return node;
    }
    pen_ast_node * val_node = new_node(WORD_NODE);
    val_node->tok = advance(p);
    node->nodes_children[2] = val_node;

    return node;
}

// <arg> ::= WORD | <stmt>
static pen_ast_node * generate_arg_node(parser * p) {
    // only treat as assignment if WORD is immediately followed by EQ
    if (check(p, WORD) &&
        p->pos + 1 < p->toks->n &&
        p->toks->toks[p->pos + 1].tok_type == EQ) {
        return generate_stmt_node(p);
    }
    pen_ast_node * node = new_node(ARG);
    node->tok = advance(p);
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

// <command> ::= <exec> <arg_list> <redirect_list>   (POSIX simple_command)
// records [tok_start, tok_end) so the executor can slice a builtin-facing
// tok_list scoped to just this command out of a possibly multi-command line
static pen_ast_node * generate_command_node(parser * p) {
    pen_ast_node * node = new_node(COMMAND);
    node->tok_start = p->pos;
    alloc_children(node, 3);
    node->nodes_children[0] = generate_exec_node(p);
    node->nodes_children[1] = generate_arg_list_node(p);
    node->nodes_children[2] = generate_redirect_list_node(p);
    node->tok_end = p->pos;
    return node;
}

// <pipe_tail> ::= PIPE <command> <pipe_tail> | ε
static pen_ast_node * generate_pipe_tail_node(parser * p) {
    pen_ast_node * node = new_node(PIPE_TAIL);
    if (check(p, PIPE)) {
        advance(p);               // consume the '|'
        alloc_children(node, 2);
        node->nodes_children[0] = generate_command_node(p);
        node->nodes_children[1] = generate_pipe_tail_node(p);
    }
    return node;                  // ε
}

// <pipe_sequence> ::= <command> <pipe_tail>   (POSIX pipe_sequence)
static pen_ast_node * generate_pipe_sequence_node(parser * p) {
    pen_ast_node * node = new_node(PIPE_SEQUENCE);
    alloc_children(node, 2);
    node->nodes_children[0] = generate_command_node(p);
    node->nodes_children[1] = generate_pipe_tail_node(p);
    return node;
}

// <pipeline> ::= BANG? <pipe_sequence>   (POSIX pipeline)
// node->tok holds the Bang token when the pipeline is negated, else NULL
static pen_ast_node * generate_pipeline_node(parser * p) {
    pen_ast_node * node = new_node(PIPELINE);
    if (check(p, BANG)) {
        node->tok = advance(p);
    }
    alloc_children(node, 1);
    node->nodes_children[0] = generate_pipe_sequence_node(p);
    return node;
}

// <and_or_tail> ::= (AND_IF|OR_IF) <pipeline> <and_or_tail> | ε
// node->tok holds the AND_IF/OR_IF operator that precedes nodes_children[0]
static pen_ast_node * generate_and_or_tail_node(parser * p) {
    pen_ast_node * node = new_node(AND_OR_TAIL);
    if (!check(p, AND_IF) && !check(p, OR_IF)) {
        return node;               // ε
    }
    node->tok = advance(p);
    alloc_children(node, 2);
    node->nodes_children[0] = generate_pipeline_node(p);   // errors (via exec) if nothing follows
    node->nodes_children[1] = generate_and_or_tail_node(p);
    return node;
}

// <and_or> ::= <pipeline> <and_or_tail>   (POSIX and_or: '&&' / '||' chain)
static pen_ast_node * generate_and_or_node(parser * p) {
    pen_ast_node * node = new_node(AND_OR);
    alloc_children(node, 2);
    node->nodes_children[0] = generate_pipeline_node(p);
    node->nodes_children[1] = generate_and_or_tail_node(p);
    return node;
}

// <list_tail> ::= (SEMI|AMP) <and_or> <list_tail> | (SEMI|AMP) | ε
// node->tok holds the separator (';' sequences, '&' backgrounds the and_or
// that precedes it); unlike and_or_tail, a trailing separator with nothing
// after it is valid ("ls &", "ls;") -- tok set but child_count stays 0
static pen_ast_node * generate_list_tail_node(parser * p) {
    pen_ast_node * node = new_node(LIST_TAIL);
    if (!check(p, SEMI) && !check(p, AMP)) {
        return node;                // ε
    }
    node->tok = advance(p);
    if (at_end(p)) {
        return node;                // trailing separator, nothing follows
    }
    alloc_children(node, 2);
    node->nodes_children[0] = generate_and_or_node(p);
    node->nodes_children[1] = generate_list_tail_node(p);
    return node;
}

// <list> ::= <and_or> <list_tail>   (POSIX list / complete_command)
static pen_ast_node * generate_list_node(parser * p) {
    pen_ast_node * node = new_node(LIST);
    alloc_children(node, 2);
    node->nodes_children[0] = generate_and_or_node(p);
    node->nodes_children[1] = generate_list_tail_node(p);
    return node;
}

// <line> ::= <list> | ε
static pen_ast_node * generate_line_node(parser * p) {
    pen_ast_node * node = new_node(LINE);
    if (!at_end(p)) {
        alloc_children(node, 1);
        node->nodes_children[0] = generate_list_node(p);
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