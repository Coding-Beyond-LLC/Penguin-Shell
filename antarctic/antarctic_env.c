//
// Created by nate on 12/24/25.
//

#include "antarctic_env.h"

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

    if (alias_table->aliases[alias_table->next_free_index].free == 1) {

        alias_table->aliases[alias_table->next_free_index].alias_name = malloc(sizeof(char) * (strlen(alias) + 1));
        memcpy(alias_table->aliases[alias_table->next_free_index].alias_name, alias, strlen(alias) + 1);

        alias_table->aliases[alias_table->next_free_index].alias_value = malloc(sizeof(char) * (strlen(value) + 1));
        memcpy(alias_table->aliases[alias_table->next_free_index].alias_value, value, sizeof(char) * (strlen(value) + 1));

        alias_table->aliases[alias_table->next_free_index].free = 0;
        update_next_free_index(alias_table, alias_table->next_free_index);
    }else {
        //TODO throw error here, halt the shell
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

char * alias_lookup(pen_alias_table * alias_table, char * alias) {
    for (int i = 0; i < alias_table->table_limit; i++) {
        if (alias_table->aliases[i].alias_name != NULL && strcmp(alias_table->aliases[i].alias_name, alias) == 0) {
            return alias_table->aliases[i].alias_value;
        }
    }
    return NULL;
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

void pen_export(const pen_tok_list * tok_list, history * hist, pen_alias_table * alias_table, size_t arg_count){

    //the scheme will be something like VAR_NAME=String
    //so to verify that it's a valid variable entry we can just do a simple
    //state machine that checks for that =

    (void)hist;

    if (arg_count < 2) {
        return;
    }

    char * new_var = *(tok_list->args + 1);

    size_t length_to_parse = strlen(new_var);

    int total_chars_parsed = 0;
    char * curr_char = new_var;
    char name[VAR_NAME_LIM] = {0};
    char value[VAR_VAL_LIM] = {0};

    //parse out the var name
    while (total_chars_parsed < length_to_parse && *(curr_char) != '=') {
        *(name + total_chars_parsed) = *(curr_char);
        total_chars_parsed = total_chars_parsed + 1;
        curr_char = curr_char + 1;
    }

    //check if the = is present
    if (*(curr_char) == '=') {
        curr_char = curr_char + 1;
        total_chars_parsed = total_chars_parsed + 1;
    }else {
        return;
    }
    //check if there are characters remaining after =
    if (total_chars_parsed >= length_to_parse) {
        return;
    }

    //parse out the var value
    int var_char_counter = 0;
    while (total_chars_parsed < strlen(new_var) && var_char_counter < VAR_VAL_LIM) {
        *(value + var_char_counter) = *(curr_char);
        var_char_counter = var_char_counter + 1;
        curr_char = curr_char + 1;
        total_chars_parsed = total_chars_parsed + 1;
    }

    //set the var value
    if (strcmp(*(tok_list->args), "xpt") == 0) {
        setenv(name, value, 1);
    }else if (strcmp(*(tok_list->args), "alias") == 0) {
        add_alias(alias_table, name, value);
    }

}

void pen_chirp(const pen_tok_list * tok_list, history * hist, pen_alias_table * alias_table, size_t arg_count) {

    (void)hist;

    if (arg_count < 2) {
        return;
    }

    char * var = *(tok_list->args + 1);
    char * value = getenv(var);
    if (value != NULL) {
        printf("(•ᴗ•)ゝ->%s\n", value);
    }else {
        printf("(•ᴖ•)ゝ Penguin couldn't find %s\n", var);
    }
}

void pen_unalias(const pen_tok_list * tok_list, history * hist, pen_alias_table * alias_table, size_t arg_count) {
    (void) hist;
    if (arg_count < 2) {
        return;
    }

    char * alias = *(tok_list->args + 1);
    clear_alias(alias_table, alias);
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

void fill_entry(history_entry ** entry, char * full_cmmd, char * command, char ** args, size_t command_len, size_t arg_count) {
    memcpy((*entry)->full_cmmd, full_cmmd, command_len);
    memcpy((*entry)->command, command, strlen(command));
    for (int i = 0; i < arg_count; i++) {
        memcpy(*((*entry)->args + i), *(args + i), strlen(*(args + i)));
    }
}

void clear_entry(history_entry * entry) {
    free(entry->full_cmmd);
    free(entry->command);
    for (int i = 0; i < entry->arg_count; i++) {
        free(*(entry->args + i));
    }
    free(entry->args);
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

void pen_print_history(const pen_tok_list * tok_list, history * hist, pen_alias_table * alias_table, size_t arg_count) {
    (void)arg_count;
    for (int i = 0; i < hist->cells_filled; i++) {
        printf("%s\n", (*(hist->entries + i))->full_cmmd);
    }
}