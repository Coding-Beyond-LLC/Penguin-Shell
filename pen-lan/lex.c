#include "lex.h"
#include <stdlib.h>
#include <string.h>

// counts the number of tokens in the input string, used to allocate memory for the token list
static size_t count_tokens(char * input, size_t n) {
    size_t count = 0;

    size_t i = 0;
    while(i < n){
        if(input[i] == ' ' || input[i] == '\t' || input[i] == '\n'){
            count++;
        }
        i++;
    }

    return n > 0 ? count + 1 : count;
}

//helper function for flushing tokens
static void flush(char * input, pen_tok_list * tok_list, size_t input_start_tok_idx, size_t tok_idx, size_t tok_len){
    pen_tok * tok = &tok_list->toks[tok_idx];

    //allocate the token and copy contents into it
    tok->text = malloc(tok_len + 1);

    char * text = tok->text;
    memcpy(text, input + input_start_tok_idx, tok_len);
    text[tok_len] = '\0';

    //set the token type
    if      (strcmp(text, "|")  == 0) tok->tok_type = PIPE;
    else if (strcmp(text, ">")  == 0) tok->tok_type = REDIRECT_OUT;
    else if (strcmp(text, "<")  == 0) tok->tok_type = REDIRECT_IN;
    else if (strcmp(text, ">>") == 0) tok->tok_type = REDIRECT_APPEND;
    else                              tok->tok_type = WORD;
}

static void set_args(pen_tok_list * tok_list) {
    tok_list->args = malloc((tok_list->n + 1) * sizeof(char *));
    for (size_t i = 0; i < tok_list->n; i++) {
        tok_list->args[i] = tok_list->toks[i].text;  // borrow the token's string
    }
    tok_list->args[tok_list->n] = NULL;              // the sentinel execvp scans for
}

pen_tok_list * tokenize(char * input, size_t n) {

    //important to do a first pass to count the number of tokens, so we can allocate memory for the token list
    size_t num_tokens = count_tokens(input, n);

    pen_tok_list * tok_list = malloc(sizeof(pen_tok_list));
    tok_list->toks = malloc(num_tokens * sizeof(pen_tok));

    size_t idx = 0;
    size_t input_start_tok_idx = 0;
    size_t tok_idx = 0;
    size_t tok_len = 0;

    // second pass to actually tokenize the input string
    while(idx < n){

        if(input[idx] == ' ' || input[idx] == '\t' || input[idx] == '\n'){

            if(tok_len > 0) {
                flush(input, tok_list, input_start_tok_idx, tok_idx, tok_len);
                tok_idx++;
            }

            //update counter values
            input_start_tok_idx = idx + 1;
            tok_len = 0;
            idx++;

            continue;
        }

        tok_len++;
        idx++;
    }

    //flush any last token
    if(tok_len > 0){
        flush(input, tok_list, input_start_tok_idx, tok_idx, tok_len);
        tok_idx++;
    }

    tok_list->n = tok_idx;
    set_args(tok_list);

    return tok_list;
}