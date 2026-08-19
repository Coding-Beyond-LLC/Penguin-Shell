#ifndef PEN_EXPAND_H
#define PEN_EXPAND_H

#include "./lan_structs.h"
#include "../antarctic/antarctic_env.h"
#include "lex.h"

pen_tok_list * expand_aliases(pen_tok_list * tok_list, pen_alias_table * alias_table);

pen_tok_list * normalize_env_var_tokens(pen_tok_list * tok_list);

char * expand_word_text(const char * text, int last_status);

#endif