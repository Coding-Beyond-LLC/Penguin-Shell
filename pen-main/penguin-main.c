//
// Created by nate on 12/22/25.
//
//Main REPL loop for the penguin shell
#include "penguin-main.h"

//SECTION: LOOKUP AND DISPATCH
//looks up the command in the built in commands and returns a pointer to the function that implements the command, if not found then returns null
pen_builtin * pen_lookup(pen_tok_list * tok_list) {

    //algo, first see if first token is less than the first builtin or greater than the last builtin, if not then binary search
    if (strcmp(tok_list->toks[0].text, "alias") >= 0 && strcmp(tok_list->toks[0].text, "xpt") <= 0) {

        //lowest and highest index of the built in commands
        int low = 0;
        int high = BUILT_INS_COUNT - 1;

        while (low <= high) {

            int mid = low + ((high - low) / 2);

            pen_builtin * builtin = &pen_builtins[mid];

            int comp_val = strcmp(tok_list->toks[0].text, builtin->command);
            if (comp_val == 0) {
                return builtin;
            }

            if (comp_val > 0) {
                low = mid + 1;
            }else {
                high = mid - 1;
            }
        }
    }

    return NULL;
}
//END SECTION: LOOKUP AND DISPATCH

//SECTION: Helper functions for main shell loop commands
//helper function for handling the help flag
void handle_help(pen_builtin * builtin) {
    fprintf(stdout, "%s", builtin->usage);
}

//method that handles execution of the commands, if the command is not found then it will print an error message
void waddle(pen_tok_list * tok_list) {

    pid_t pid;
    int child_status;

    pid = fork();

    //child executes here
    if (pid < 0) {
        printf("Error executing child! errno: %d\n", errno);
        exit(-1);
    }

    if (pid == 0) {
        //execvp call here
        execvp(tok_list->toks[0].text, tok_list->args);
        fprintf(stderr, "%s: %s\n", tok_list->toks[0].text, strerror(errno));
        _exit(errno == ENOENT ? 127 : 126);
    }

    wait(&child_status);
}

//method that implements the exit built in command, exits the shell and does some clean up before exiting
void pen_exit(pen_tok_list * tok_list, history * hist, pen_alias_table * alias_table, size_t arg_count) {
    clean_up(hist, alias_table);
    free_tokens(tok_list, arg_count);
    printf("\033[38;2;0;255;255m" "Goodbye (•ᴗ•)ゝ\n" "\033[0m");
    exit(0);
}

//method that implements the pwd built in command, prints the current working directory to the user
void pen_pwd(pen_tok_list * tok_list, history * hist, pen_alias_table * alias_table, size_t arg_count) {
    (void)hist;
    (void)arg_count;
    char cwd[MAX_PATH_LEN] = {0};
    getcwd(cwd, MAX_PATH_LEN);
    printf("%s\n", cwd);
}

//method that implements the cd built in command, changes the current working directory to the specified path, if no path is specified then it changes to the home directory
void pen_cd(pen_tok_list * tok_list, history * hist, pen_alias_table * alias_table, size_t arg_count) {
    (void)hist;
    int cd_res = -1;
    if (arg_count < 2 || strcmp(tok_list->toks[1].text, "*") == 0) {
        char * home_dir = getenv("HOME");
        cd_res = chdir(home_dir);
        if (cd_res == -1) {
            exit(-1);
        }
    }else {
        cd_res = chdir(tok_list->toks[1].text);
        if (cd_res == -1) {
            printf("Could not find directory or no such directory exists. :(\n");
        }
    }

}
//END SECTION: Helper functions for main shell loop commands

//SECTION: Main shell loop commands
//method that parses the options for the shell itself, such as the help flag, if the user enters -h or --help then it will print the usage message and exit
static void parse_options(int argc, char ** argv) {
    int option_char = 0;
    while (((option_char) = getopt_long(argc, argv, "h::", pen_options, NULL)) != -1) {
        switch (option_char) {
            case 'h':
                fprintf(stdout, "%s", USAGE);
                exit(0);
                break;
            default:
                fprintf(stdout, "%s", USAGE);
                exit(1);
        }
    }
}

//method that prints the welcome message when the shell is first run
void greet() {
    printf("===========================\n");
    printf("    P  E  N  G  U  I  N    \n");
    printf("===========================\n");
}

static int is_help_flag(const char * arg) {
    return arg && (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0);
}

static void record_history(history *hist, char *cmmd, pen_tok_list * tok_list, size_t arg_count) {

    if (*cmmd) add_history(cmmd); // this adds the command to the readline history in order to support the up and down arrow keys for navigating through command history

    if(strcmp(tok_list->toks[0].text, "history") == 0) return;
    add_to_history(hist, cmmd, tok_list->toks[0].text, tok_list->args, strlen(cmmd), arg_count);
}

static void dispatch_command(pen_tok_list * tok_list, history * hist, pen_alias_table * alias_table, size_t arg_count) {

    pen_builtin * builtin = pen_lookup(tok_list);

    if(builtin == NULL) {
        waddle(tok_list);
        return;
    }

    if(arg_count > 1 && is_help_flag(tok_list->toks[1].text)) {
        handle_help(builtin);
        return;
    }

    builtin->pen_func(tok_list, hist, alias_table, arg_count);
}

static void process_line(char * cmmd, history * hist, pen_alias_table * alias_table) {

    pen_tok_list * tok_list = tokenize(cmmd, strlen(cmmd));

    //empty command, just free the tokens and return
    if(tok_list->n == 0) {
        free_tokens(tok_list, tok_list->n);
        return;
    }

    record_history(hist, cmmd, tok_list, tok_list->n);
    dispatch_command(tok_list, hist, alias_table, tok_list->n);
    free_tokens(tok_list, tok_list->n);
}

static char * build_prompt(char * prompt) {

    char cwd[MAX_PATH_LEN] = {0};

    //get the current working directory
    getcwd(cwd, MAX_PATH_LEN);

    //print the prompt with the current working directory
    snprintf(prompt, MAX_PATH_LEN + 38, "\033[38;2;0;255;255m" "%s (•ᴗ•)ゝ " "\033[0m", cwd);

    return prompt;
}

//method that handles cleaning up the resources taken by the shell before exiting, such as the history and alias table
int clean_up(history * hist, pen_alias_table * alias_table) {
    for (int i = 0; i < hist->cells_filled; i++) {
        history_entry * entry = hist->entries[i];
        clear_entry(entry);
        free(entry);
    }
    free(hist->entries);
    free(hist);
    clear_alias_table(alias_table);
    return 0;
}

//method that frees the resources taken by the tokens, such as the memory allocated for the tokens and the memory allocated for the token strings
void free_tokens(pen_tok_list * tok_list, size_t arg_count) {
    for (int i = 0; i < arg_count; i++) {
        free(tok_list->toks[i].text);
    }
    free(tok_list->toks);
    free(tok_list->args);
    free(tok_list);
}
//END SECTION: Main shell loop commands

//SECTION: Main method
//main method that runs the shell, handles the main REPL loop and calls the appropriate methods to execute the commands entered by the user
int run(int argc, char ** argv) {

    parse_options(argc, argv);

    greet();

    //initialize the history and alias table
    history * hist = init_history();
    pen_alias_table * alias_table = init_alias_table();

    //main REPL loop
    char * cmmd;
    char prompt[MAX_PATH_LEN + 38];

    while ((cmmd = readline(build_prompt(prompt))) != NULL) {
        process_line(cmmd, hist, alias_table);
        free(cmmd);
    }

    clean_up(hist, alias_table);
    return 0;
}
//END SECTION: Main method