#include "lex.h"
#include "lan_structs.h"
#include <stdlib.h>
#include <string.h>

// Operators this shell recognizes. '=' isn't POSIX -- it's this shell's
// WORD EQ WORD assignment grammar (see parse.c generate_stmt_node) -- but it
// slots into the same "operator" machinery as the real ones.
typedef struct {
    const char * text;
    pen_tok_type type;
} pen_operator;

static const pen_operator OPERATORS[] = {
    { "<<-", DLESSDASH },
    { ">>",  REDIRECT_APPEND },
    { "<<",  DLESS },
    { "&&",  AND_IF },
    { "||",  OR_IF },
    { ";;",  DSEMI },
    { "<&",  LESSAND },
    { ">&",  GREATAND },
    { "<>",  LESSGREAT },
    { ">|",  CLOBBER },
    { "|",   PIPE },
    { "<",   REDIRECT_IN },
    { ">",   REDIRECT_OUT },
    { ";",   SEMI },
    { "&",   AMP },
    { "=",   EQ },
};
#define NUM_OPERATORS (sizeof(OPERATORS) / sizeof(OPERATORS[0]))

// Reserved words (POSIX XCU 2.4). '!' has no operator-table entry above --
// unlike '&' or ';' it isn't punctuation, so the word scanner just reads it
// like any other character and it arrives here as an ordinary WORD "!".
typedef struct {
    const char * text;
    pen_tok_type type;
} pen_reserved_word;

static const pen_reserved_word RESERVED_WORDS[] = {
    { "!",     BANG   },
    { "{",     LBRACE },
    { "}",     RBRACE },
    { "case",  CASE   },
    { "do",    DO     },
    { "done",  DONE   },
    { "elif",  ELIF   },
    { "else",  ELSE   },
    { "esac",  ESAC   },
    { "fi",    FI     },
    { "for",   FOR    },
    { "if",    IF     },
    { "in",    IN     },
    { "then",  THEN   },
    { "until", UNTIL  },
    { "while", WHILE  },
};
#define NUM_RESERVED_WORDS (sizeof(RESERVED_WORDS) / sizeof(RESERVED_WORDS[0]))

// true if a token of this type is one after which the grammar expects the
// start of a new command (i.e. reserved-word recognition applies to the
// token that follows) -- separator_op/AND_IF/OR_IF/PIPE begin a new
// and_or/pipeline/command, and Bang precedes the pipe_sequence it negates
static int starts_command_position(pen_tok_type t) {
    switch (t) {
        case SEMI: case AMP: case AND_IF: case OR_IF: case PIPE: case BANG:
            return 1;
        default:
            return 0;
    }
}

// Shell Grammar Rules, rule 1: a WORD is recognized as a reserved word only
// where a reserved word could be the next correct token. We approximate
// that "command name" position as the first token of the line or the token
// right after an operator that starts a new command; a reserved word's text
// appearing anywhere else (e.g. as a plain argument) stays a WORD.
static void reclassify_reserved_words(pen_tok_list * tok_list) {
    int command_position = 1;
    for (size_t i = 0; i < tok_list->n; i++) {
        pen_tok * tok = &tok_list->toks[i];
        if (command_position && tok->tok_type == WORD) {
            for (size_t j = 0; j < NUM_RESERVED_WORDS; j++) {
                if (strcmp(tok->text, RESERVED_WORDS[j].text) == 0) {
                    tok->tok_type = RESERVED_WORDS[j].type;
                    break;
                }
            }
        }
        command_position = starts_command_position(tok->tok_type);
    }
}

// true if c could be the first character of some operator (a prefix match,
// not necessarily a complete one -- e.g. '&' alone, pending a second '&')
static int is_operator_start(char c) {
    for (size_t i = 0; i < NUM_OPERATORS; i++)
        if (OPERATORS[i].text[0] == c) return 1;
    return 0;
}

// longest operator that matches input starting at idx, or 0 if none completes
static size_t operator_match(const char * input, size_t n, size_t idx, pen_tok_type * type_out) {
    size_t best_len = 0;
    pen_tok_type best_type = WORD;
    for (size_t i = 0; i < NUM_OPERATORS; i++) {
        size_t len = strlen(OPERATORS[i].text);
        if (len <= best_len) continue;
        if (idx + len <= n && strncmp(input + idx, OPERATORS[i].text, len) == 0) {
            best_len = len;
            best_type = OPERATORS[i].type;
        }
    }
    if (type_out) *type_out = best_type;
    return best_len;
}

static size_t scan_substitution(const char * input, size_t n, size_t idx);

// idx points at the opening '`'; returns index just past the matching close
static size_t scan_backtick(const char * input, size_t n, size_t idx) {
    size_t i = idx + 1;
    while (i < n && input[i] != '`') {
        if (input[i] == '\\' && i + 1 < n) i += 2;
        else i++;
    }
    return i < n ? i + 1 : n;
}

// idx points at '$', with input[idx+1] being '(' or '{'; returns index just
// past the matching close, honoring nested quotes/substitutions along the way
static size_t scan_substitution(const char * input, size_t n, size_t idx) {
    char open = input[idx + 1];
    char close = (open == '(') ? ')' : '}';
    size_t i = idx + 2;
    int depth = 1;
    int sq = 0, dq = 0;

    while (i < n && depth > 0) {
        char c = input[i];
        if (sq) {
            if (c == '\'') sq = 0;
            i++;
            continue;
        }
        if (!dq && c == '\\')  { i += (i + 1 < n) ? 2 : 1; continue; }
        if (!dq && c == '\'')  { sq = 1; i++; continue; }
        if (c == '"')          { dq = !dq; i++; continue; }
        if (!dq && c == '`')   { i = scan_backtick(input, n, i); continue; }
        if (!dq && c == '$' && i + 1 < n && (input[i + 1] == '(' || input[i + 1] == '{')) {
            i = scan_substitution(input, n, i);
            continue;
        }
        if (!dq && c == open)  { depth++; i++; continue; }
        if (!dq && c == close) { depth--; i++; continue; }
        i++;
    }
    return i;
}

// Recognizes one token starting at *idx, per POSIX shell token recognition
// rules, and advances *idx past it. Returns 0 at end of input (rule 1).
// Quoting/backslash (rule 4) only govern what delimits the token here; the
// quote and escaping-backslash markers themselves are folded out of the
// text this function builds rather than deferred to a separate removal
// pass, matching this codebase's existing single-pass lexer design.
// Substitutions ($(...), ${...}, `...`, rule 5) are kept whole and verbatim.
static int next_token(char * input, size_t n, size_t * idx, char ** text_out, pen_tok_type * type_out) {

    // skip blanks, and '#' comments up to (excluding) the next newline (rule 9)
    for (;;) {
        while (*idx < n && (input[*idx] == ' ' || input[*idx] == '\t' || input[*idx] == '\n'))
            (*idx)++;

        if (*idx < n && input[*idx] == '#') {
            while (*idx < n && input[*idx] != '\n') (*idx)++;
            continue;
        }
        break;
    }

    if (*idx >= n) return 0;

    char * out = text_out ? malloc((n - *idx) + 1) : NULL;
    size_t out_len = 0;

    // rule 6: an unquoted char that can start an operator delimits whatever
    // came before and begins a new operator token; rules 2/3 (extend while
    // possible, delimit once nothing longer matches) fall out of taking the
    // longest match in one shot
    pen_tok_type op_type;
    size_t op_len = operator_match(input, n, *idx, &op_type);
    if (op_len > 0) {
        if (out) { memcpy(out, input + *idx, op_len); out[op_len] = '\0'; }
        if (type_out) *type_out = op_type;
        *idx += op_len;
        if (text_out) *text_out = out;
        return 1;
    }

    // otherwise, a word: consume until a blank or unquoted operator-start
    // char delimits it (rules 7/8/10), honoring quoting/substitution along
    // the way (rules 4/5)
    int in_squote = 0, in_dquote = 0;
    while (*idx < n) {
        char c = input[*idx];

        if (in_squote) {
            (*idx)++;
            if (c == '\'') { in_squote = 0; continue; }  // closing marker: not copied (quote removal)
            if (out) out[out_len] = c;
            out_len++;
            continue;
        }

        if (c == '\\') {
            // backslash-newline is a line join: dropped entirely, not kept
            if (*idx + 1 < n && input[*idx + 1] == '\n') { *idx += 2; continue; }
            (*idx)++;
            // the backslash itself is a quoting marker, not content (quote removal);
            // only the character it protects survives into the token
            if (*idx < n) {
                if (out) out[out_len] = input[*idx];
                out_len++;
                (*idx)++;
            }
            continue;
        }

        if (!in_dquote && c == '\'') {
            in_squote = 1;
            (*idx)++;
            continue;  // opening marker: not copied
        }

        if (c == '"') {
            in_dquote = !in_dquote;
            (*idx)++;
            continue;  // quote marker: not copied
        }

        if (c == '$' && *idx + 1 < n && (input[*idx + 1] == '(' || input[*idx + 1] == '{')) {
            size_t end = scan_substitution(input, n, *idx);
            if (out) memcpy(out + out_len, input + *idx, end - *idx);
            out_len += end - *idx;
            *idx = end;
            continue;
        }

        if (c == '`') {
            size_t end = scan_backtick(input, n, *idx);
            if (out) memcpy(out + out_len, input + *idx, end - *idx);
            out_len += end - *idx;
            *idx = end;
            continue;
        }

        if (in_dquote) {
            if (out) out[out_len] = c;
            out_len++;
            (*idx)++;
            continue;
        }

        if (c == ' ' || c == '\t' || c == '\n') break;

        // out_len > 0 guard: a char that merely *can start* an operator (e.g.
        // lone '&', prefix of "&&") but didn't complete one at operator_match
        // above must still be consumed as a literal on its own -- otherwise
        // it would delimit an empty word and the scan would never advance
        if (out_len > 0 && is_operator_start(c)) break;

        if (out) out[out_len] = c;
        out_len++;
        (*idx)++;
    }

    if (out) { out[out_len] = '\0'; *text_out = out; }
    if (type_out) *type_out = WORD;
    return 1;
}

static size_t count_tokens(char * input, size_t n) {
    size_t count = 0;
    size_t idx = 0;
    while (next_token(input, n, &idx, NULL, NULL)) count++;
    return count;
}

static void set_args(pen_tok_list * tok_list) {
    tok_list->args = malloc((tok_list->n + 1) * sizeof(char *));
    for (size_t i = 0; i < tok_list->n; i++) {
        tok_list->args[i] = tok_list->toks[i].text;  // borrow the token's string
    }
    tok_list->args[tok_list->n] = NULL;              // the sentinel execvp scans for
}

pen_tok_list * tokenize(char * input, size_t n) {

    // first pass: count tokens so the array can be allocated up front
    size_t num_tokens = count_tokens(input, n);

    pen_tok_list * tok_list = malloc(sizeof(pen_tok_list));
    tok_list->toks = malloc(num_tokens * sizeof(pen_tok));
    tok_list->n = 0;

    // second pass: build the tokens for real
    size_t idx = 0;
    char * text;
    pen_tok_type type;
    while (next_token(input, n, &idx, &text, &type)) {
        pen_tok * tok = &tok_list->toks[tok_list->n++];
        tok->text = text;
        tok->tok_type = (type == WORD && text[0] == '$') ? ENV_VAR : type;
    }

    reclassify_reserved_words(tok_list);
    set_args(tok_list);

    return tok_list;
}
