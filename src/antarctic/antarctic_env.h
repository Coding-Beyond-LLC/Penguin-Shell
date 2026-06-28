//
// Created by nate on 12/24/25.
//

//this file will be used to manage the underlying environment of the penguin shell

#ifndef PENGUIN_ANTARCTIC_ENV_H
#define PENGUIN_ANTARCTIC_ENV_H

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <pwd.h>
#include "../pen-lan/lex.h"

//common constants used by penguin go here
#define MAX_CMMD_LEN 65536
#define MAX_PATH_LEN 262144
#define MAX_ARG_LEN 128
#define MAX_ARGS 1024
#define TOK_LIM 1024
#define VAR_NAME_LIM 256
#define VAR_VAL_LIM 1024
#define HISTORY_LIM 128
#define INITIAL_ALIAS_LIM 32
#define ALIAS_ARG_LIM 16

typedef struct {
    char * full_cmmd;
    char * command;
    char ** args;
    int arg_count;
    int full_cmmd_len;
} history_entry ;

typedef struct {
    int next_empty;
    int cells_filled;
    history_entry ** entries;
} history;

typedef struct {
    int free;
    char * alias_name;
    char * alias_value;
} alias_t;

typedef struct {
    alias_t aliases[INITIAL_ALIAS_LIM];
    size_t alias_count;
    size_t table_limit;
    int next_free_index;
} pen_alias_table;

//environment manipulation library
void pen_export(pen_tok_list * tok_list, history * hist, pen_alias_table * alias_table, const size_t arg_count);
void pen_chirp(pen_tok_list * tok_list, history * hist, pen_alias_table * alias_table, const size_t arg_count);
void pen_unalias(pen_tok_list * tok_list, history * hist, pen_alias_table * alias_table, const size_t arg_count);

void print_antarctic_vars();

//history management library
history * init_history();

history_entry * init_entry(size_t command_len, size_t arg_count);

void fill_entry(history_entry ** entry, char * full_cmmd, char * command, char ** args, size_t command_len, size_t arg_count);

int add_to_history(history * hist, char * full_cmmd, char * command, char ** args, size_t command_len, size_t arg_count);

void pen_print_history(pen_tok_list * tok_list, history * hist, pen_alias_table * alias_table, size_t arg_count);

void clean_history(history * hist);

pen_alias_table * init_alias_table();

//looks up existing aliases
char * alias_lookup(pen_alias_table * alias_table, char * alias);

void add_alias(pen_alias_table * alias_table, char * alias, char * value);

void clear_alias(pen_alias_table * alias_table, char * alias);

void clear_alias_table(pen_alias_table * alias_table);

void update_next_free_index(pen_alias_table * alias_table, int last_free_index);

#endif //PENGUIN_ANTARCTIC_ENV_H