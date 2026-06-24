#include "expand.h"

pen_tok_list * expand_aliases(pen_tok_list * tok_list, pen_alias_table * alias_table) {

    pen_tok_list * expanded_tok_list = NULL;

    char input[4096];

    for(size_t i = 0; i < tok_list->n; i++){
        char * alias = alias_lookup(alias_table, tok_list->toks[i].text);

        if(alias != NULL){
            strcat(input, alias);
        }else{
            strcat(input, tok_list->toks[i].text);
            if(tok_list->toks[i].tok_type == WORD || tok_list->toks[i].tok_type == PIPE || 
            tok_list->toks[i].tok_type == REDIRECT_IN || tok_list->toks[i].tok_type == REDIRECT_OUT ||
            tok_list->toks[i].tok_type == REDIRECT_APPEND) strcat(input, " ");
        }
    }

    expanded_tok_list = tokenize(input, strlen(input));
    return expanded_tok_list;
}