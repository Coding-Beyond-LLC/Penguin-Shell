//
// Created by nate on 12/22/25.
//

#ifndef PENGUIN_PENGUIN_MAIN_H
#define PENGUIN_PENGUIN_MAIN_H

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/wait.h>
#include <getopt.h>
#include <fcntl.h>
#include <readline/readline.h>
#include <readline/history.h>
#include "../antarctic/antarctic_env.h"
#include "../pen-lan/lex.h"
#include "../pen-lan/parse.h"

//methods to handle build in shell commands
void pen_exit(const pen_tok_list * tok_list, history * hist, pen_alias_table * alias_table, size_t arg_count);
void pen_pwd(const pen_tok_list * tok_list, history * hist, pen_alias_table * alias_table, size_t arg_count);
void pen_cd(const pen_tok_list * tok_list, history * hist, pen_alias_table * alias_table, size_t arg_count);

#endif //PENGUIN_PENGUIN_MAIN_H