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
#include <readline/readline.h>
#include <readline/history.h>
#include "../antarctic/antarctic_env.h"
#include "../pen-lan/lex.h"

#define USAGE   \
    "The penguin shell (•ᴗ•)ゝ\n" \
    "Run the shell by calling the penguin executable, be sure to set penguin in your path to call from any dir. \n"\
    "basic commands:\n"\
    "  alias [alias name]=[alias value]      Creates an alias with the specified name.\n" \
    "  cd [path]                             Change directory to path (use * for home directory).\n" \
    "  chirp [environment variable]          Outputs out the value of the specified environment variable.\n" \
    "  exit                                  Closes the shell.\n"\
    "  history                               Outputs command history throughout the shell's runtime up to a max of 128 commands (latest commands).\n" \
    "  pwd                                   Outputs the current working directory.\n" \
    "  unalias [alias name]                  Deletes the specified alias.\n" \
    "  xpt [variable name]=[value]           Sets a new environment variable with the specificied value.\n" \

#define BUILT_INS_COUNT (sizeof(pen_builtins) / sizeof(pen_builtin))

static struct option pen_options[] = {
    {"help", no_argument, NULL, 'h'}
};

typedef struct {
    char * command;
    void (*pen_func)(pen_tok_list * tok_list, history * hist, pen_alias_table * alias_table, size_t arg_count);
    char * usage;
} pen_builtin;

//method that handles execution of the commands
void waddle(pen_tok_list * tok_list);

//methods to handle build in shell commands
void pen_exit(pen_tok_list * tok_list, history * hist, pen_alias_table * alias_table, size_t arg_count);
void pen_pwd(pen_tok_list * tok_list, history * hist, pen_alias_table * alias_table, size_t arg_count);
void pen_cd(pen_tok_list * tok_list, history * hist, pen_alias_table * alias_table, size_t arg_count);

//main shell loop commands
void greet();
int clean_up(history * hist, pen_alias_table * alias_table);
void free_tokens(pen_tok_list * tok_list, size_t arg_count);
int run(int argc, char ** argv);

static pen_builtin pen_builtins[] = {
    {"alias", pen_export, "alias [alias name]=[alias value], Creates an alias with the specified name.\n"},
    {"cd", pen_cd, "cd [path], Change directory to path (use * for home directory).\n"},
    { "chirp", pen_chirp, "chirp [environment variable], Outputs out the value of the specified environment variable.\n"},
    {"exit", pen_exit, "exit, Closes the shell.\n"},
    { "history", pen_print_history, "history, Outputs command history throughout the shell's runtime up to a max of 128 commands (latest commands).\n"},
    { "pwd", pen_pwd, "pwd, Outputs the current working directory.\n"},
    {"unalias", pen_unalias, "unalias [alias name], deletes the specified alias.\n"},
    { "xpt", pen_export, "xpt [variable name]=[value], Sets a new environment variable with the specified value.\n"}
};

pen_builtin * pen_lookup(pen_tok_list * tok_list);
void handle_help(pen_builtin * builtin);
#endif //PENGUIN_PENGUIN_MAIN_H