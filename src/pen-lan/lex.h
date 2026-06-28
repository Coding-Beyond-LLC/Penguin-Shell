#ifndef PEN_LEX_H
#define PEN_LEX_H

#include <stddef.h>
#include "./lan_structs.h"

pen_tok_list * tokenize(char * input, size_t n);

#endif