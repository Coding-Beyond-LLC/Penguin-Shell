#include "expand.h"
 
pen_tok_list * expand_aliases(pen_tok_list * tok_list, pen_alias_table * alias_table) {
 
    pen_tok_list * expanded_tok_list = NULL;
 
    char input[4096] = {0};   // must be zeroed before the first strcat
 
    // unalias takes an alias *name* as its operand -- don't expand it out from under pen_unalias.
    // (Band-aid: the real rule is "only expand in command position"; see note below.)
    int is_unalias = tok_list->n > 0 && strcmp(tok_list->toks[0].text, "unalias") == 0;
 
    for(size_t i = 0; i < tok_list->n; i++){
        char * alias = (is_unalias && i > 0)
                           ? NULL
                           : alias_lookup(alias_table, tok_list->toks[i].text);
 
        if(alias != NULL){
            strcat(input, alias);
            strcat(input, " ");                       // alias value is a fresh word boundary
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
 
