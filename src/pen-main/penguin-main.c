//
// Copyright (c) 2026 Coding Beyond LLC. All rights reserved.
//
//Main REPL loop for the penguin shell
#include "penguin-main.h"
#include "../pen-util/pen-util.h"
#include <bits/getopt_ext.h>
#include <git2/deprecated.h>
#include <git2/global.h>
#include <git2/refs.h>
#include <git2/repository.h>
#include <readline/history.h>
#include <string.h>

#define USAGE   \
    "The penguin shell (•ᴗ•)ゝ\n" \
    "Run the shell by calling the penguin executable, be sure to set penguin in your path to call from any dir. \n" \
    "options: -h (print help), -x (turn off writing of history to disk), -d (turn on debug mode). \n" \
    "basic commands:\n" \
    "  alias [alias name]=[alias value]      Creates an alias with the specified name.\n" \
    "  cd [path]                             Change directory to path (use * for home directory).\n" \
    "  exit/quit                             Closes the shell.\n"\
    "  greet                                 Outputs the shell's startup banner.\n" \
    "  history                               Outputs command history throughout the shell's runtime up to a max of 128 commands (latest commands).\n" \
    "  pwd                                   Outputs the current working directory.\n" \
    "  unalias [alias name]                  Deletes the specified alias.\n" \
    "  xpt [variable name]=[value]           Sets a new environment variable with the specificied value.\n" \

#define ALIAS_USG "alias [alias name]=[alias value], Creates an alias with the specified name.\n"
#define CD_USG "cd [path], Change directory to path (use * for home directory).\n"
#define EXIT_USG "exit, Closes the shell.\n"
#define GREET_USG "greet, Outputs the shell's startup banner.\n"
#define HIST_USG "history, Outputs command history throughout the shell's runtime up to a max of 128 commands (latest commands).\n"
#define PWD_USG "pwd, Outputs the current working directory.\n"
#define UNALIAS_USG "unalias [alias name], deletes the specified alias.\n"
#define XPT_USG "xpt [variable name]=[value], Sets a new environment variable with the specified value.\n"

#define BUILT_INS_COUNT (sizeof(pen_builtins) / sizeof(pen_builtin))
#define PEN_BUILTIN_KEY(b) ((b)->command)

static int pen_should_exit = 0;

// exit status of the last foreground list item, exposed to the next line's
// $?/${?} expansion (see process_line); a backgrounded item ("cmd &") never
// touches this -- matching real shells, its status is only visible via wait
static int pen_last_status = 0;

static struct option pen_options[] = {
    {"help", no_argument, NULL, 'h'},
    {"no-disk-hist", no_argument, NULL, 'x'},
    {"debug-mode", no_argument, NULL, 'd'},
    {0, 0, 0, 0}
};

typedef struct {
    char * command;
    void (*pen_func)(pen_tok_list * tok_list, history * hist, pen_alias_table * alias_table, const size_t arg_count);
    char * usage;
} pen_builtin;

static pen_builtin pen_builtins[] = {
    {"alias", pen_export, ALIAS_USG},
    {"cd", pen_cd, CD_USG},
    {"exit", pen_exit, EXIT_USG},
    {"greet", pen_greet, GREET_USG},
    {"help", pen_help, USAGE},
    { "history", pen_print_history, HIST_USG},
    {"quit", pen_exit, EXIT_USG},
    { "pwd", pen_pwd, PWD_USG},
    {"unalias", pen_unalias, UNALIAS_USG},
    { "xpt", pen_export, XPT_USG}
};


//SECTION: LOOKUP AND DISPATCH
static pen_builtin * pen_lookup(const pen_tok_list * tok_list) {
    pen_builtin * result;
    BINARY_SEARCH(pen_builtins, BUILT_INS_COUNT, tok_list->toks[0].text, PEN_BUILTIN_KEY, result);
    return result;
}
//END SECTION: LOOKUP AND DISPATCH

//SECTION: Helper functions for main shell loop commands
//helper function for handling the help flag
static void handle_help(const pen_builtin * builtin) {
    fprintf(stdout, "%s", builtin->usage);
}

static char ** flatten_command(const pen_ast_node * command); // defined below

// frees an argv built by flatten_command: every entry is now an owned,
// freshly expanded copy (see expand_word_text), not borrowed from the AST
static void free_argv(char ** argv) {
    for (size_t i = 0; argv[i] != NULL; i++) free(argv[i]);
    free(argv);
}

static void apply_redirects(const pen_ast_node * redirect_list) {
    const pen_ast_node * cur = redirect_list;
    while (cur->child_count > 0) {
        const pen_ast_node * redirect  = cur->nodes_children[0];
        const char *         filename  = redirect->nodes_children[0]->tok->text;
        pen_tok_type         rtype     = redirect->tok->tok_type;

        int fd;
        if (rtype == REDIRECT_IN) {
            fd = open(filename, O_RDONLY);
        } else if (rtype == REDIRECT_OUT) {
            fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        } else {
            fd = open(filename, O_WRONLY | O_CREAT | O_APPEND, 0644);
        }

        if (fd < 0) {
            fprintf(stderr, "%s: %s\n", filename, strerror(errno));
            _exit(1);
        }

        dup2(fd, rtype == REDIRECT_IN ? STDIN_FILENO : STDOUT_FILENO);
        close(fd);

        cur = cur->nodes_children[1];
    }
}

//method that handles execution of the commands, if the command is not found then it will print an error message;
//returns the child's exit status (or 1 if it died abnormally) so &&/|| can decide whether to short-circuit
static int waddle(const pen_ast_node * command) {

    char ** argv = flatten_command(command);
    pid_t pid = fork();

    if (pid < 0) {
        printf("Error executing child! errno: %d\n", errno);
        free_argv(argv);
        return 1;
    }

    if (pid == 0) {
        apply_redirects(command->nodes_children[2]);
        execvp(argv[0], argv);
        fprintf(stderr, "%s: %s\n", argv[0], strerror(errno));
        _exit(errno == ENOENT ? 127 : 126);
    }

    free_argv(argv);
    int child_status;
    waitpid(pid, &child_status, 0);
    return WIFEXITED(child_status) ? WEXITSTATUS(child_status) : 1;
}


//method that frees the resources taken by the tokens, such as the memory allocated for the tokens and the memory allocated for the token strings
static void free_tokens(pen_tok_list * tok_list) {
    for (int i = 0; i < tok_list->n; i++) {
        free(tok_list->toks[i].text);
    }
    free(tok_list->toks);
    free(tok_list->args);
    free(tok_list);
}

//method that implements the exit built in command, exits the shell and does some clean up before exiting
void pen_exit(pen_tok_list * tok_list, history * hist, pen_alias_table * alias_table, const size_t arg_count) {
    (void)tok_list; (void)hist; (void)alias_table; (void)arg_count;
    pen_should_exit = 1;
    printf("\033[38;2;0;255;255m" "Goodbye (•ᴗ•)ゝ\n" "\033[0m");
}

//method that implements the pwd built in command, prints the current working directory to the user
void pen_pwd(pen_tok_list * tok_list, history * hist, pen_alias_table * alias_table, const size_t arg_count) {
    (void)hist;
    (void)arg_count;
    char cwd[MAX_PATH_LEN] = {0};
    getcwd(cwd, MAX_PATH_LEN);
    printf("%s\n", cwd);
}

//method that implements the cd built in command, changes the current working directory to the specified path, if no path is specified then it changes to the home directory
void pen_cd(pen_tok_list * tok_list, history * hist, pen_alias_table * alias_table, const size_t arg_count) {
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

void pen_help(pen_tok_list * tok_list, history * hist, pen_alias_table * alias_table, size_t arg_count){
    (void) tok_list;
    (void) hist;
    (void) alias_table;
    (void) arg_count;
    fprintf(stdout, "%s", USAGE);
}
//END SECTION: Helper functions for main shell loop commands

//SECTION: Main shell loop commands
//method that prints the welcome message when the shell is first run
void pen_greet(pen_tok_list * tok_list, history * hist, pen_alias_table * alias_table, const size_t arg_count) {

    (void) tok_list;
    (void) hist;
    (void) alias_table;
    (void) arg_count;

    printf(
        "+-------------------------------------------------+\n"
        "|                                                 |\n"
        "|  ####   #####  #   #   ####  #   #  ###  #   #  |\n"
        "|  #   #  #      ##  #  #      #   #   #   ##  #  |\n"
        "|  ####   ###    # # #  #  ##  #   #   #   # # #  |\n"
        "|  #      #      #  ##  #   #  #   #   #   #  ##  |\n"
        "|  #      #####  #   #   ####   ###   ###  #   #  |\n"
        "|                                                 |\n"
        "|           (c) 2026 Coding Beyond LLC            |\n"
        "|                                                 |\n"
        "+-------------------------------------------------+\n"
    );
}

static int is_help_flag(const char * arg) {
    return arg && (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0);
}

static void record_history(history *hist, char *cmmd, pen_tok_list * tok_list, size_t arg_count) {

    if (*cmmd) add_history(cmmd); // this adds the command to the readline history in order to support the up and down arrow keys for navigating through command history
    add_to_history(hist, cmmd, tok_list->toks[0].text, tok_list->args, strlen(cmmd), arg_count);

}

// Builds a builtin-facing pen_tok_list scoped to just this command's own
// tokens (see pen_ast_node.tok_start/tok_end) -- necessary now that a line
// can hold several commands ("cd /tmp; ls"): builtins like pen_export index
// tok_list->toks[] and check tok_list->n directly, so handing them the
// whole line's token list would let a later command's tokens leak into an
// earlier builtin's view of its own arguments. Each token's text is also
// expanded here (${VAR}/${?}/$?), using the *current* pen_last_status, so a
// builtin sees the same freshly-resolved values a real command's argv would.
static pen_tok_list command_tok_slice(const pen_ast_node * command, pen_tok_list * tok_list) {
    size_t n = command->tok_end - command->tok_start;
    pen_tok_list slice;
    slice.n = n;
    slice.args = NULL;          // unused by any builtin; record_history uses the whole-line tok_list instead
    slice.toks = malloc(sizeof(pen_tok) * n);
    for (size_t i = 0; i < n; i++) {
        pen_tok * src = &tok_list->toks[command->tok_start + i];
        slice.toks[i].text     = expand_word_text(src->text, pen_last_status);
        slice.toks[i].tok_type = src->tok_type;
    }
    return slice;
}

static void free_command_tok_slice(pen_tok_list * slice) {
    for (size_t i = 0; i < slice->n; i++) free(slice->toks[i].text);
    free(slice->toks);
}

//dispatches a single simple_command (builtin or exec'd); returns its exit status so &&/|| can decide whether to short-circuit
static int dispatch_command(const pen_ast_node * command, pen_tok_list * tok_list, history * hist, pen_alias_table * alias_table) {

    pen_tok_list slice = command_tok_slice(command, tok_list);
    pen_builtin * builtin = pen_lookup(&slice);
    int status;

    if(builtin == NULL) {
        status = waddle(command);
    } else if(slice.n > 1 && is_help_flag(slice.toks[1].text)) {
        handle_help(builtin);
        status = 0;
    } else {
        builtin->pen_func(&slice, hist, alias_table, slice.n);
        status = 0;             // builtins don't report a distinct exit status today
    }

    free_command_tok_slice(&slice);
    return status;
}

//counts how many command stages the pipe_sequence holds by walking the PIPE_TAIL chain
static size_t pipe_sequence_stage_count(const pen_ast_node * pipe_sequence) {
    size_t count = 1;                                            // the leading <command>
    const pen_ast_node * tail = pipe_sequence->nodes_children[1]; // <pipe_tail>
    while (tail->child_count > 0) {                              // PIPE <command> <pipe_tail>
        count++;
        tail = tail->nodes_children[1];                          // step to the next <pipe_tail>
    }
    return count;
}

static size_t count_args(const pen_ast_node * arg_list) {
    size_t count = 0;
    const pen_ast_node * cur = arg_list;
    while (cur->child_count > 0) {
        count++;
        cur = cur->nodes_children[1];
    }
    return count;
}

// Returns a malloc'd NULL-terminated argv; every entry is a freshly expanded
// (${VAR}/${?}/$?, via the current pen_last_status) owned copy -- free with
// free_argv, not a bare free(argv)
static char ** flatten_command(const pen_ast_node * command) {
    const pen_ast_node * exec_node = command->nodes_children[0];
    const pen_ast_node * arg_list  = command->nodes_children[1];
    size_t argc = 1 + count_args(arg_list);

    char ** argv = malloc(sizeof(char *) * (argc + 1));
    argv[0] = expand_word_text(exec_node->tok->text, pen_last_status);
    const pen_ast_node * cur = arg_list;
    for (size_t i = 1; i < argc; i++) {
        argv[i] = expand_word_text(cur->nodes_children[0]->tok->text, pen_last_status);
        cur = cur->nodes_children[1];
    }
    argv[argc] = NULL;
    return argv;
}

//runs a multi-stage pipe_sequence; returns the last stage's exit status (POSIX: a pipeline's status is its last command's)
static int execute_pipe_sequence(const pen_ast_node * pipe_sequence, size_t stage_count) {
    pen_ast_node ** commands = malloc(sizeof(pen_ast_node *) * stage_count);
    commands[0] = pipe_sequence->nodes_children[0];
    const pen_ast_node * tail = pipe_sequence->nodes_children[1];
    for (size_t i = 1; i < stage_count; i++) {
        commands[i] = tail->nodes_children[0];
        tail = tail->nodes_children[1];
    }

    int (*pipes)[2] = malloc(sizeof(int[2]) * (stage_count - 1));
    for (size_t i = 0; i < stage_count - 1; i++) {
        if (pipe(pipes[i]) < 0) {
            fprintf(stderr, "penguin: pipe: %s\n", strerror(errno));
            free(commands);
            free(pipes);
            return 1;
        }
    }

    pid_t * pids = malloc(sizeof(pid_t) * stage_count);

    for (size_t i = 0; i < stage_count; i++) {
        char ** argv = flatten_command(commands[i]);
        pids[i] = fork();
        if (pids[i] < 0) {
            fprintf(stderr, "penguin: fork: %s\n", strerror(errno));
            free_argv(argv);
            break;
        }
        if (pids[i] == 0) {
            if (i > 0)               dup2(pipes[i - 1][0], STDIN_FILENO);
            if (i < stage_count - 1) dup2(pipes[i][1],     STDOUT_FILENO);
            for (size_t j = 0; j < stage_count - 1; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }
            apply_redirects(commands[i]->nodes_children[2]);
            execvp(argv[0], argv);
            fprintf(stderr, "%s: %s\n", argv[0], strerror(errno));
            _exit(errno == ENOENT ? 127 : 126);
        }
        free_argv(argv);
    }

    for (size_t i = 0; i < stage_count - 1; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }
    int last_status = 1;
    for (size_t i = 0; i < stage_count; i++) {
        int status;
        waitpid(pids[i], &status, 0);
        if (i == stage_count - 1) last_status = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
    }

    free(commands);
    free(pipes);
    free(pids);
    return last_status;
}

//runs a <pipeline>: dispatches its pipe_sequence, then applies '!' negation; returns the (possibly negated) exit status.
//updates pen_last_status here (not just once per line) so $?/${?} in a later command -- even one on the
//same line, or later in the same and_or chain -- see the freshest value, matching real shell semantics
static int execute_pipeline(const pen_ast_node * pipeline, pen_tok_list * tok_list, history * hist, pen_alias_table * alias_table) {
    const pen_ast_node * pipe_sequence = pipeline->nodes_children[0];
    size_t stages = pipe_sequence_stage_count(pipe_sequence);

    int status;
    if (stages == 1) {
        status = dispatch_command(pipe_sequence->nodes_children[0], tok_list, hist, alias_table);
    } else {
        status = execute_pipe_sequence(pipe_sequence, stages);
    }

    if (pipeline->tok != NULL) status = (status == 0) ? 1 : 0;   // leading Bang negates
    pen_last_status = status;
    return status;
}

//runs an <and_or> chain, short-circuiting on '&&' (only run next on success) / '||' (only on failure); returns the last pipeline run's status
static int execute_and_or(const pen_ast_node * and_or, pen_tok_list * tok_list, history * hist, pen_alias_table * alias_table) {
    int status = execute_pipeline(and_or->nodes_children[0], tok_list, hist, alias_table);

    const pen_ast_node * tail = and_or->nodes_children[1];
    while (tail->child_count > 0) {                      // (AND_IF|OR_IF) <pipeline> <and_or_tail>
        int run_next = (tail->tok->tok_type == AND_IF) ? (status == 0) : (status != 0);
        if (run_next) status = execute_pipeline(tail->nodes_children[0], tok_list, hist, alias_table);
        tail = tail->nodes_children[1];
    }
    return status;
}

//runs one <list> item, backgrounding it behind a fork when followed by '&' so the shell doesn't
//block on this and_or chain's internal (synchronous) &&/|| evaluation -- there's no job control here,
//just enough indirection that "a && b &" doesn't stall the prompt waiting on 'a'
static void execute_list_item(const pen_ast_node * and_or, pen_tok_list * tok_list, history * hist, pen_alias_table * alias_table, int background) {
    if (!background) {
        execute_and_or(and_or, tok_list, hist, alias_table);   // updates pen_last_status internally (see execute_pipeline)
        return;
    }

    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "penguin: fork: %s\n", strerror(errno));
        return;
    }
    if (pid == 0) {
        execute_and_or(and_or, tok_list, hist, alias_table);
        _exit(0);
    }
}

//walks a <list>: and_or (separator_op and_or)*, running each and_or in order (';') or detached ('&')
static void execute_list(const pen_ast_node * list, pen_tok_list * tok_list, history * hist, pen_alias_table * alias_table) {
    const pen_ast_node * and_or = list->nodes_children[0];
    const pen_ast_node * tail   = list->nodes_children[1];

    for (;;) {
        int background = tail->tok != NULL && tail->tok->tok_type == AMP;
        execute_list_item(and_or, tok_list, hist, alias_table, background);

        if (tail->child_count == 0) break;   // ε, or a trailing separator with nothing after it
        and_or = tail->nodes_children[0];
        tail   = tail->nodes_children[1];
    }
}

//walks the parsed line and runs it
static void execute_line(pen_ast_node * line, pen_tok_list * tok_list, history * hist, pen_alias_table * alias_table) {
    if (line->child_count == 0) return;                     // empty line, nothing to run
    execute_list(line->nodes_children[0], tok_list, hist, alias_table);
}

static void process_line(char * cmmd, history * hist, pen_alias_table * alias_table, int is_pen_rc) {

    pen_tok_list * tok_list = tokenize(cmmd, strlen(cmmd));

    //blank line, just free the tokens and return
    if(tok_list->n == 0) {
        free_tokens(tok_list);
        return;
    }

    pen_tok_list * expanded_tok_list = NULL;

    if(strcmp(tok_list->toks[0].text, "alias") != 0) expanded_tok_list = expand_aliases(tok_list, alias_table);
    // structural only: retypes ENV_VAR tokens the grammar should accept as a WORD (e.g.
    // "${FOO}", "$?") so parsing succeeds. The actual value substitution happens per-command
    // at execution time (flatten_command / command_tok_slice), not here -- so $?/${?} reflect
    // the freshest exit status even for a later command on the same line ("false; echo ${?}").
    pen_tok_list * expanded_tok_list_with_vars = expanded_tok_list == NULL
        ? normalize_env_var_tokens(tok_list)
        : normalize_env_var_tokens(expanded_tok_list);

    //record every non-empty line, the way a shell keeps everything you typed
    //we record the original input pre variable resolution
    if(is_pen_rc != 1) record_history(hist, cmmd, tok_list, tok_list->n);

    pen_ast_node * line = parse(expanded_tok_list_with_vars);
    if (line == NULL) {                     // "| ls", "ls |", or a token the grammar can't take yet
        if(debug_mode_flag) fprintf(stderr, "penguin: syntax error\n");
        free_tokens(tok_list);
        free_tokens(expanded_tok_list);
        free_tokens(expanded_tok_list_with_vars);
        return;
    }

    execute_line(line, expanded_tok_list_with_vars, hist, alias_table);

    free_ast(line);                         // frees the nodes (token text is borrowed, not freed here)
    free_tokens(tok_list);
    if(expanded_tok_list != NULL) free_tokens(expanded_tok_list);     // frees the token text and the backing arrays
    free_tokens(expanded_tok_list_with_vars);
}

static void process_rc(history * hist, pen_alias_table * alias_table){

    const char * home = getenv("HOME");
    if (home == NULL) {
        struct passwd *pwd = getpwuid(getuid());
        if (pwd != NULL) home = pwd->pw_dir;
    }

    if (home != NULL) {

        char * pen_rc_file_name = malloc(strlen(home) + 8);   // "/.penrc" (7 chars) + NUL
        strcpy(pen_rc_file_name, home);
        strcat(pen_rc_file_name, "/.penrc");

        int fd = open(pen_rc_file_name, O_RDWR, 0777);

        if(fd != -1){

            FILE * stream = fdopen(fd, "r");

            if(stream == NULL && debug_mode_flag){
                printf("failed to read line from .penrc :(\n");
            }else{

                char * line = NULL;
                size_t len = 0;

                while(getline(&line, &len, stream) != -1){

                    line[strcspn(line, "\n")] = '\0';

                    process_line(line, hist, alias_table, 1);
                }

                free(line);
                fclose(stream);
            }
        }

        close(fd);
        free(pen_rc_file_name);

    }

}

char * get_git_branch(char * cwd){

    // Buffer to hold the path of the found repository root
    git_buf root_path = {0};

    git_repository * repo = NULL;
    git_reference * head = NULL;
    char * branch = NULL;

    int res = git_repository_discover(&root_path, cwd, 0, NULL);

    //open the repository
    if(git_repository_open(&repo, root_path.ptr) != 0){
        return NULL;
    }

    //git_repository_head resolves HEAD to the branch tip it points at (a direct
    //reference), so no symbolic-ref check is needed here
    if(git_repository_head(&head, repo) == 0){
        //duplicate the name since it's owned by head, which we free below
        branch = strdup(git_reference_shorthand(head));
    }

    git_reference_free(head);
    git_repository_free(repo);
    git_buf_free(&root_path);

    return branch;

}

static char * build_prompt(char * prompt) {

    char cwd[MAX_PATH_LEN] = {0};

    //get the current working directory
    getcwd(cwd, MAX_PATH_LEN);

    char * git_branch = get_git_branch(cwd);

    char user[MAX_PATH_LEN] = {0};
    char host[MAX_PATH_LEN] = {0};

    //get host name
    gethostname(host, sizeof(host));

    //get user id
    uid_t uid = getuid();
    struct passwd *pw = getpwuid(uid);

    //build the git branch segment, colored, only if we're inside a repo
    char git_segment[128] = {0};
    if(git_branch != NULL){
        snprintf(git_segment, sizeof(git_segment), "\001\033[38;2;133;50;168m\002(%.32s)\001\033[38;2;0;255;255m\002 ", git_branch);
    }

    //print the prompt with the current working directory
    snprintf(prompt, MAX_PATH_LEN + 256 + strlen(git_segment), "\001\033[38;2;0;255;255m\002" "%.32s@%.32s#%s (•ᴗ•)ゝ %s" "\001\033[0m\002", pw->pw_name, host, cwd, git_segment);

    free(git_branch);

    return prompt;
}

//method that parses the options for the shell itself, such as the help flag, if the user enters -h or --help then it will print the usage message and exit
static void parse_options(const int argc, char ** argv, history * hist, pen_alias_table * alias_table) {
    int option_char = 0;
    int opt_idx = 0;
    while (((option_char) = getopt_long(argc, argv, "h::x::d::", pen_options, &opt_idx)) != -1) {
        switch (option_char) {
            case 'h':
                fprintf(stdout, "%s", USAGE);
                exit(0);
                break;
            case 'x':
                persist_hist_off_flag = 1;
                break;
            case 'd':
                debug_mode_flag = 1;
                process_line("alias lla=\"ls -la\"", hist, alias_table, 1);
                process_line("alias ll=\"ls -ll\"", hist, alias_table, 1);
                process_line("xpt PEN_HOME=${HOME}/Penguin-Shell", hist, alias_table, 1);
                break;
            default:
                fprintf(stdout, "%s", USAGE);
                exit(1);
        }
    }
}

//END SECTION: Main shell loop commands

//SECTION: Main method
//main method that runs the shell, handles the main REPL loop and calls the appropriate methods to execute the commands entered by the user
int run(int argc, char ** argv, history * hist, pen_alias_table * alias_table) {

    //Initialize git tools
    git_libgit2_init();

    parse_options(argc, argv, hist, alias_table);

    process_rc(hist, alias_table);

    pen_greet(NULL, NULL, NULL, 0);

    //main REPL loop
    char * cmmd;
    char prompt[MAX_PATH_LEN + 38];

    while (!pen_should_exit && (cmmd = readline(build_prompt(prompt))) != NULL) {
        process_line(cmmd, hist, alias_table, 0);
        free(cmmd);
    }

    return 0;
}
//END SECTION: Main method