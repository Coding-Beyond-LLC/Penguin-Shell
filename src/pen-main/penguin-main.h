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
#include <sys/types.h>
#include <getopt.h>
#include <fcntl.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <pwd.h>
#include "../antarctic/antarctic_env.h"
#include "../pen-lan/lex.h"
#include "../pen-lan/parse.h"
#include "../pen-lan/expand.h"

//methods to handle build in shell commands
void pen_exit(pen_tok_list * tok_list, history * hist, pen_alias_table * alias_table, size_t arg_count);
void pen_pwd(pen_tok_list * tok_list, history * hist, pen_alias_table * alias_table, size_t arg_count);
void pen_cd(pen_tok_list * tok_list, history * hist, pen_alias_table * alias_table, size_t arg_count);
void pen_help(pen_tok_list * tok_list, history * hist, pen_alias_table * alias_table, size_t arg_count);

int run(int argc, char ** argv, history * hist, pen_alias_table * alias_table);

#endif //PENGUIN_PENGUIN_MAIN_H