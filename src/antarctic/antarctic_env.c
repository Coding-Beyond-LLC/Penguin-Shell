//
// Copyright (c) 2026 Coding Beyond LLC. All rights reserved.
//

#include <readline/history.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include "antarctic_env.h"
#include "../pen-lan/lex.h"

extern char ** environ;

int persist_hist_off_flag = 0;
int debug_mode_flag = 0;

pen_alias_table * init_alias_table() {
    pen_alias_table * alias_table = malloc(sizeof(pen_alias_table));
    alias_table->alias_count = 0;
    alias_table->table_limit = INITIAL_ALIAS_LIM;
    alias_table->next_free_index = 0;

    for (int i = 0; i < INITIAL_ALIAS_LIM; i++) {
        alias_table->aliases[i].alias_name = NULL;
        alias_table->aliases[i].alias_value = NULL;
        alias_table->aliases[i].free = 1;
    }

    return alias_table;
}

void add_alias(pen_alias_table * alias_table, char * alias, char * value) {

    int existing_idx = alias_lookup(alias_table, alias);

    if (alias_table->aliases[alias_table->next_free_index].free == 1 && existing_idx == -1) {

        alias_table->aliases[alias_table->next_free_index].alias_name = malloc(sizeof(char) * (strlen(alias) + 1));
        memcpy(alias_table->aliases[alias_table->next_free_index].alias_name, alias, strlen(alias) + 1);

        alias_table->aliases[alias_table->next_free_index].alias_value = malloc(sizeof(char) * (strlen(value) + 1));
        memcpy(alias_table->aliases[alias_table->next_free_index].alias_value, value, sizeof(char) * (strlen(value) + 1));

        alias_table->aliases[alias_table->next_free_index].free = 0;
        update_next_free_index(alias_table, alias_table->next_free_index);

    }else if(existing_idx != -1){
        free(alias_table->aliases[existing_idx].alias_value);
        alias_table->aliases[existing_idx].alias_value = malloc(sizeof(char) * (strlen(value) + 1));
        memcpy(alias_table->aliases[existing_idx].alias_value, value, sizeof(char) * (strlen(value) + 1));
    }

}

void update_next_free_index(pen_alias_table * alias_table, int last_free_index) {

    if (last_free_index < alias_table->table_limit - 1 && alias_table->aliases[last_free_index + 1].free == 1) {
        alias_table->next_free_index = last_free_index + 1;
    }else {
        for (int i = 0; i < alias_table->table_limit; i++) {
            if (alias_table->aliases[i].free == 1) {
                alias_table->next_free_index = i;
                return;
            }
        }
        //TODO throw error here, halt the shell
    }

}

int alias_lookup(pen_alias_table * alias_table, char * alias) {
    for (int i = 0; i < alias_table->table_limit; i++) {
        if (alias_table->aliases[i].alias_name != NULL && strcmp(alias_table->aliases[i].alias_name, alias) == 0) {
            return i;
        }
    }
    return -1;
}

void clear_alias(pen_alias_table * alias_table, char * alias) {
    for (int i = 0; i < alias_table->table_limit; i++) {
        if (alias_table->aliases[i].alias_name != NULL && strcmp(alias_table->aliases[i].alias_name, alias) == 0) {
            free(alias_table->aliases[i].alias_name);
            alias_table->aliases[i].alias_name = NULL;
            free(alias_table->aliases[i].alias_value);
            alias_table->aliases[i].alias_value = NULL;
            alias_table->aliases[i].free = 1;
        }
    }
}

void clear_alias_table(pen_alias_table * alias_table) {
    for (int i = 0; i < alias_table->table_limit; i++) {
        if (alias_table->aliases[i].free == 0) {
            free(alias_table->aliases[i].alias_name);
            alias_table->aliases[i].alias_name = NULL;
            free(alias_table->aliases[i].alias_value);
            alias_table->aliases[i].alias_value = NULL;
        }
    }
    free(alias_table);
}

void pen_export(pen_tok_list * tok_list, history * hist, pen_alias_table * alias_table, size_t arg_count){

    //the scheme will be something like VAR_NAME=String
    //so to verify that it's a valid variable entry we can just do a simple
    //state machine that checks for that =

    (void)hist;

    if (tok_list->n < 4) {
        return;
    }

    //set the var value
    if (strcmp(tok_list->toks[0].text, "xpt") == 0) {
        setenv(tok_list->toks[1].text, tok_list->toks[3].text, 1);
    }else if (strcmp(tok_list->toks[0].text, "alias") == 0) {
        add_alias(alias_table, tok_list->toks[1].text, tok_list->toks[3].text);
    }

}

void pen_unset(pen_tok_list * tok_list, history * hist, pen_alias_table * alias_table, size_t arg_count){

    (void) hist;
    (void) alias_table;
    (void) arg_count;

    if(tok_list->n < 2){
        return;
    }

    unsetenv(tok_list->toks[1].text);
}

void pen_unalias(pen_tok_list * tok_list, history * hist, pen_alias_table * alias_table, size_t arg_count) {
    (void) hist;
    if (tok_list->n < 2) {
        return;
    }

    char * alias = tok_list->toks[1].text;
    clear_alias(alias_table, alias);
}

void pen_print_antarctic_vars(pen_tok_list * tok_list, history * hist, pen_alias_table * alias_table, size_t arg_count){

    char ** antarctic_env_ptr;

    for(antarctic_env_ptr = environ; *antarctic_env_ptr != NULL; antarctic_env_ptr++){
        puts(*antarctic_env_ptr);
    }

}

//history management library
history * init_history() {
    history * hist = malloc(sizeof(history));
    hist->next_empty = 0;
    hist->cells_filled = 0;
    hist->entries = malloc(sizeof(history_entry *) * HISTORY_LIM);
    for (int i = 0; i < HISTORY_LIM; i++) {
        *(hist->entries + i) = NULL;
    }

    const char * home = getenv("HOME");
    if (home == NULL) {
        struct passwd *pwd = getpwuid(getuid());
        if (pwd != NULL) home = pwd->pw_dir;
    }

    if (home != NULL) {

        char * pen_hist_file_name = malloc(strlen(home) + 14);
        strcpy(pen_hist_file_name, home);
        strcat(pen_hist_file_name, "/.pen_history");

        int fd = open(pen_hist_file_name, O_RDWR, 0666);

        if(fd == -1){
            printf("No .pen_history file, creating new one :)\n");
        }else{

            FILE * stream = fdopen(fd, "r");

            if(stream == NULL){
                printf("Failed to read line from .pen_history :(\n");
            }else{

                char * line = NULL;
                size_t len = 0;

                while(getline(&line, &len, stream) != -1){

                    line[strcspn(line, "\n")] = '\0';

                    pen_tok_list * tok_list = tokenize(line, strlen(line));

                    if (tok_list->n > 0) {
                        add_history(line);
                        add_to_history(hist, line, tok_list->toks[0].text,
                            tok_list->args, strlen(line), tok_list->n);
                    }

                    for (size_t i = 0; i < tok_list->n; i++) free(tok_list->toks[i].text);

                    free(tok_list->toks);
                    free(tok_list->args);
                    free(tok_list);
                }
                free(line);
                fclose(stream);
            }
        }

        close(fd);
        free(pen_hist_file_name);

    }

    return hist;
}

history_entry * init_entry(size_t command_len, size_t arg_count) {
    history_entry * new_entry = malloc(sizeof(history_entry));

    new_entry->full_cmmd = NULL;
    new_entry->command = NULL;
    new_entry->args = NULL;

    //allocate memory for the full command
    new_entry->full_cmmd = malloc(sizeof(char) * command_len + 1);
    memset(new_entry->full_cmmd, 0, sizeof(char) * command_len + 1);

    //allocate memory for the command portion
    new_entry->command = malloc(sizeof(char) * MAX_CMMD_LEN + 1);
    memset(new_entry->command, 0, sizeof(char) * MAX_CMMD_LEN + 1);

    //allocate memory for the arguments
    new_entry->args = malloc(sizeof(char *) * arg_count);
    for (int i = 0; i < arg_count; i++) {
        *(new_entry->args + i) = malloc(sizeof(char) * MAX_ARG_LEN);
        memset(*(new_entry->args + i), 0, sizeof(char) * MAX_ARG_LEN);
    }

    return new_entry;
}

static void persist_history(history * hist){

    if(persist_hist_off_flag == 1){
        return;
    }

    const char * home = getenv("HOME");
    if (home == NULL) {
        struct passwd *pwd = getpwuid(getuid());
        if (pwd != NULL) home = pwd->pw_dir;
    }

    if (home != NULL) {

        char * pen_hist_file_name = malloc(strlen(home) + 14);
        strcpy(pen_hist_file_name, home);
        strcat(pen_hist_file_name, "/.pen_history");

        int fd = open(pen_hist_file_name, O_CREAT | O_RDWR, 0666);

        if(fd == -1){
            printf("Error saving history %d\n", errno);
        }else{
            if(ftruncate(fd, 0) == - 1){
                printf("Error saving history %d\n", errno);
            }

            if(fd != -1){
                //store oldest section of history
                for(size_t i = hist->next_empty; i < hist->cells_filled; i++){
                    if(hist->entries[i]->full_cmmd != NULL) {
                        write(fd, hist->entries[i]->full_cmmd, strlen(hist->entries[i]->full_cmmd));
                        write(fd, "\n", 1);
                    }
                }

                //store newer section of history
                for(size_t i = 0; i < hist->next_empty; i++){
                    if(hist->entries[i]->full_cmmd != NULL){
                        write(fd, hist->entries[i]->full_cmmd, strlen(hist->entries[i]->full_cmmd));
                        write(fd, "\n", 1);
                    }
                }
            }

            close(fd);
        }

        free(pen_hist_file_name);
    }


}

void fill_entry(history_entry ** entry, char * full_cmmd, char * command, char ** args, size_t command_len, size_t arg_count) {
    memcpy((*entry)->full_cmmd, full_cmmd, command_len);
    memcpy((*entry)->command, command, strlen(command));
    for (int i = 0; i < arg_count; i++) {
        memcpy(*((*entry)->args + i), *(args + i), strlen(*(args + i)));
    }
}

static void clear_entry(history_entry * entry) {
    free(entry->full_cmmd);
    free(entry->command);
    for (int i = 0; i < entry->arg_count; i++) {
        free(*(entry->args + i));
    }
    free(entry->args);
}

void clean_history(history * hist){
    persist_history(hist);
    for (int i = 0; i < hist->cells_filled; i++) {
        history_entry * entry = hist->entries[i];
        clear_entry(entry);
        free(entry);
    }
    free(hist->entries);
    free(hist);
}



//TODO handle duplicate or empty case
int add_to_history(history * hist, char * full_cmmd, char * command, char ** args, size_t command_len, size_t arg_count) {

    //set the next history entry to fill (can be one that's already in use, in that case it gets overwritten)
    history_entry ** next_entry = hist->entries + hist->next_empty;

    //if the entry is null then we allocate a new entry into memory
    if ((*next_entry) == NULL) {
        (*next_entry) = init_entry(command_len, arg_count);
    } else {
        //clear out the full command, command, and arguments
        clear_entry((*next_entry));
        (*next_entry) = init_entry(command_len, arg_count);
    }

    //deep copy the full command, command, and arguments
    fill_entry(next_entry, full_cmmd, command, args, command_len, arg_count);

    //set the argument count
    (*next_entry)->arg_count = arg_count;

    //set the full command length
    (*next_entry)->full_cmmd_len = command_len;

    //increase the cells filled count, if we're at the limit then don't increase
    if (hist->cells_filled < HISTORY_LIM) {
        hist->cells_filled = hist->cells_filled + 1;
    }

    //specify the next entry to be filled
    hist->next_empty = (hist->next_empty + 1) % HISTORY_LIM;

    return 0;
}

void pen_print_history(pen_tok_list * tok_list, history * hist, pen_alias_table * alias_table, size_t arg_count) {
    (void)arg_count;

    for (int i = hist->next_empty; i < hist->cells_filled; i++) {
        printf("%s\n", hist->entries[i]->full_cmmd);
    }

    for (int i = 0; i < hist->next_empty; i++) {
        printf("%s\n", hist->entries[i]->full_cmmd);
    }
}