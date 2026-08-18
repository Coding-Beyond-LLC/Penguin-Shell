#include "expand.h"
#include "lan_structs.h"
#include <stdlib.h>
#include <string.h>

// local string dup so we don't depend on strdup's feature-test macros under -std=c23
static char * dup_str(const char * s) {
    size_t len = strlen(s);
    char * p = malloc(len + 1);
    memcpy(p, s, len + 1);
    return p;
}

// append one finished token (owned text + type) to a growing tok_list, doubling capacity as needed
static void push_tok(pen_tok_list * out, size_t * cap, char * text, pen_tok_type type) {
    if (out->n == *cap) {
        *cap = (*cap == 0) ? 8 : *cap * 2;
        out->toks = realloc(out->toks, *cap * sizeof(pen_tok));
    }
    out->toks[out->n].text = text;      // ownership transfers to `out`
    out->toks[out->n].tok_type = type;
    out->n++;
}
 
pen_tok_list * expand_aliases(pen_tok_list * tok_list, pen_alias_table * alias_table) {
 
    pen_tok_list * out = malloc(sizeof(pen_tok_list));
    out->toks = NULL;
    out->args = NULL;
    out->n = 0;
    size_t cap = 0;
 
    // unalias takes an alias *name* as its operand -- don't expand it out from under pen_unalias.
    // (Still a band-aid for the lack of command-position-only expansion; see note in chat.)
    int is_unalias = tok_list->n > 0 && strcmp(tok_list->toks[0].text, "unalias") == 0;
 
    for (size_t i = 0; i < tok_list->n; i++) {
        int existing_idx = (is_unalias && i > 0)
                           ? -1
                           : alias_lookup(alias_table, tok_list->toks[i].text);

        if (existing_idx != -1) {
            // re-lex just the alias value; its tokens (and their types) splice straight in,
            // so an alias whose value contains operators (e.g. "git push | tee log") works.
            pen_tok_list * sub = tokenize(alias_table->aliases[existing_idx].alias_value, strlen(alias_table->aliases[existing_idx].alias_value));
            for (size_t j = 0; j < sub->n; j++) {
                push_tok(out, &cap, sub->toks[j].text, sub->toks[j].tok_type); // take ownership of text
            }
            free(sub->toks);   // array only -- the text pointers were transferred to `out`
            free(sub->args);
            free(sub);
        } else {
            // copy the original token verbatim: grouping (e.g. "ll=ls -l") is preserved
            push_tok(out, &cap, dup_str(tok_list->toks[i].text), tok_list->toks[i].tok_type);
        }
    }
 
    // build the NULL-terminated args view execvp wants, borrowing the token strings
    out->args = malloc((out->n + 1) * sizeof(char *));
    for (size_t i = 0; i < out->n; i++) {
        out->args[i] = out->toks[i].text;
    }
    out->args[out->n] = NULL;
 
    return out;
}

// Expand all ${VAR} references in `str`, returning a newly malloc'd string.
// Unset variables expand to empty string (standard shell behaviour).
// ${?} is the one special case: it's shell state (last exit status), not an
// environment variable, so it's substituted directly instead of via getenv.
static char * expand_vars_in_str(const char * str, int last_status) {
    size_t out_cap = 256;
    size_t out_len = 0;
    char * out = malloc(out_cap);

    const char * p = str;
    while (*p) {
        if (*p == '$' && *(p + 1) == '{') {
            const char * name_start = p + 2;
            size_t name_len = 0;
            while (name_start[name_len] && name_start[name_len] != '}')
                name_len++;

            char * name = malloc(name_len + 1);
            memcpy(name, name_start, name_len);
            name[name_len] = '\0';
            char status_buf[12];
            const char * val;
            if (strcmp(name, "?") == 0) {
                snprintf(status_buf, sizeof(status_buf), "%d", last_status);
                val = status_buf;
            } else {
                val = getenv(name);
            }
            free(name);

            if (val) {
                size_t val_len = strlen(val);
                while (out_len + val_len + 1 > out_cap) {
                    out_cap *= 2;
                    out = realloc(out, out_cap);
                }
                memcpy(out + out_len, val, val_len);
                out_len += val_len;
            }
            p = name_start + name_len;
            if (*p == '}') p++;
        } else {
            if (out_len + 2 > out_cap) {
                out_cap *= 2;
                out = realloc(out, out_cap);
            }
            out[out_len++] = *p++;
        }
    }
    out[out_len] = '\0';
    return out;
}

// true for a token whose *text alone* (an exact "$?", or anything containing
// a "${...}" reference) the grammar should accept as a WORD -- independent
// of what value it will substitute to, which isn't decided until execution
static int is_var_reference(const char * text) {
    if (strcmp(text, "$?") == 0) return 1;
    const char * brace = strstr(text, "${");
    return brace != NULL && strchr(brace, '}') != NULL;
}

// Structural pass, run once before parsing: retypes ENV_VAR tokens that the
// grammar should accept as a WORD -- exact "$?" and anything containing a
// "${...}" reference -- without substituting a value yet. Substitution
// happens later, per-command, right before that command executes (see
// expand_word_text), so $?/${?} reflect the freshest exit status even for a
// later command on the same line ("false; echo ${?}"). A bare $FOO with no
// braces is left as ENV_VAR and so still fails to parse -- this shell
// requires braces around variable references, by design.
pen_tok_list * normalize_env_var_tokens(pen_tok_list * tok_list) {

    pen_tok_list * out = malloc(sizeof(pen_tok_list));
    out->toks = NULL;
    out->args = NULL;
    out->n = 0;
    size_t cap = 0;

    for (size_t i = 0; i < tok_list->n; i++) {
        const char * text = tok_list->toks[i].text;
        pen_tok_type type = tok_list->toks[i].tok_type;

        if (type == ENV_VAR && is_var_reference(text)) type = WORD;

        push_tok(out, &cap, dup_str(text), type);
    }

    // build the NULL-terminated args view execvp wants, borrowing the token strings
    out->args = malloc((out->n + 1) * sizeof(char *));
    for (size_t i = 0; i < out->n; i++) {
        out->args[i] = out->toks[i].text;
    }
    out->args[out->n] = NULL;

    return out;
}

// Substitutes ${VAR}/${?} references (and the bare "$?" spelling) in a
// single token's text, using last_status for $?/${?}. Called per-command at
// execution time -- not once for the whole line -- so a later command on
// the same line sees an earlier one's exit status. Always returns a freshly
// malloc'd string, even when there was nothing to substitute.
char * expand_word_text(const char * text, int last_status) {
    if (strcmp(text, "$?") == 0) {
        char buf[12];
        snprintf(buf, sizeof(buf), "%d", last_status);
        return dup_str(buf);
    }
    const char * var_start = strstr(text, "${");
    if (var_start != NULL && strchr(var_start, '}') != NULL) {
        return expand_vars_in_str(text, last_status);
    }
    return dup_str(text);
}