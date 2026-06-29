#ifndef PEN_EXPAND_H
#define PEN_EXPAND_H

#include "./lan_structs.h"
#include "../antarctic/antarctic_env.h"
#include "lex.h"

pen_tok_list * expand_aliases(pen_tok_list * tok_list, pen_alias_table * alias_table);

pen_tok_list * expand_env_vars(pen_tok_list * tok_list);

#endif