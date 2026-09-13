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
#include <pwd.h>
#include <readline/history.h>
#include <string.h>
#include <unistd.h>

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

// shared by build_prompt's snprintf and its caller's buffer declaration --
// large enough for a resolved PS1 (template + a full MAX_PATH_LEN cwd) plus
// the git segment and reset code appended after it
#define PROMPT_BUF_LEN (MAX_PATH_LEN + 256)

// placeholder PS1 resolves at render time (see build_prompt) so the prompt
// tracks the live working directory instead of a snapshot baked in at boot
#define PS1_CWD_TOKEN "{cwd}"

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
    { "pwd", pen_pwd, PWD_USG},
    {"quit", pen_exit, EXIT_USG},
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
    (void)tok_list;
    (void)hist;
    (void)alias_table;
    (void)arg_count;
    char * pwd = getenv("PWD");
    if (pwd == NULL) {
        char cwd[MAX_PATH_LEN] = {0};
        getcwd(cwd, MAX_PATH_LEN);
        printf("%s\n", cwd);
    } else {
        printf("%s\n", pwd);
    }
}

//method that implements the cd built in command, changes the current working directory to the specified path, if no path is specified then it changes to the home directory
void pen_cd(pen_tok_list * tok_list, history * hist, pen_alias_table * alias_table, const size_t arg_count) {
    (void)hist;
    int cd_res = -1;
    if (arg_count < 2 || strcmp(tok_list->toks[1].text, "~") == 0) {
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

    if (cd_res == 0) {
        char cwd[MAX_PATH_LEN] = {0};
        if (getcwd(cwd, MAX_PATH_LEN) != NULL) {
            setenv("PWD", cwd, 1);
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

// Expands one arg_list entry to its argv text. Usually a plain ARG leaf
// (->tok set), but generate_arg_node also promotes a "WORD=WORD" argument
// (e.g. the "x=y" in "echo x=y") to a STMT node -- the same shape used for
// xpt/alias's own VAR=VALUE argument, but xpt/alias read it via raw tokens
// (command_tok_slice), not this AST, so a STMT here is just a literal
// "var=value" argument to whatever command it belongs to; STMT itself has no
// ->tok (only its var/op/val children do), so it's reassembled from those.
static char * flatten_arg(const pen_ast_node * arg) {
    if (arg->node_type == STMT) {
        char * var = expand_word_text(arg->nodes_children[0]->tok->text, pen_last_status);
        char * val = expand_word_text(arg->nodes_children[2]->tok->text, pen_last_status);
        char * out = malloc(strlen(var) + strlen(val) + 2);   // "var" + '=' + "val" + '\0'
        sprintf(out, "%s=%s", var, val);
        free(var);
        free(val);
        return out;
    }
    return expand_word_text(arg->tok->text, pen_last_status);
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
        argv[i] = flatten_arg(cur->nodes_children[0]);
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

// forward declaration: execute_list is defined further down, but the
// compound-command executors below (a loop/if body is itself a <list>) need
// to call it, and they in turn are needed by execute_pipeline above that
static void execute_list(const pen_ast_node * list, pen_tok_list * tok_list, history * hist, pen_alias_table * alias_table);

// runs a compound_list (an if/while/until condition or body, or a for
// body) and returns the exit status of the last pipeline it actually ran --
// execute_pipeline keeps pen_last_status current on every pipeline it runs,
// including nested ones, so reading it back here after the list finishes
// gives exactly the "status of the last command run" POSIX wants
static int execute_compound_list_status(const pen_ast_node * list, pen_tok_list * tok_list, history * hist, pen_alias_table * alias_table) {
    execute_list(list, tok_list, hist, alias_table);
    return pen_last_status;
}

// runs an IF_CLAUSE node: nodes_children = [condition, then-body, else-part],
// where else-part is NULL (no else), a LIST (plain 'else'), or another
// IF_CLAUSE (an 'elif', see generate_else_part_node) -- recurses for that case
static int execute_if_clause(const pen_ast_node * node, pen_tok_list * tok_list, history * hist, pen_alias_table * alias_table) {
    int cond_status = execute_compound_list_status(node->nodes_children[0], tok_list, hist, alias_table);
    int status;

    if (cond_status == 0) {
        status = execute_compound_list_status(node->nodes_children[1], tok_list, hist, alias_table);
    } else if (node->nodes_children[2] != NULL) {
        const pen_ast_node * else_part = node->nodes_children[2];
        status = (else_part->node_type == IF_CLAUSE)
            ? execute_if_clause(else_part, tok_list, hist, alias_table)
            : execute_compound_list_status(else_part, tok_list, hist, alias_table);
    } else {
        status = 0;   // no branch taken -- POSIX: exit status is zero
    }

    pen_last_status = status;
    return status;
}

// runs a WHILE_CLAUSE (negate=0) or UNTIL_CLAUSE (negate=1) node: nodes_children = [condition, body].
// pen_should_exit is checked after each iteration so 'exit' inside a loop body actually stops the
// loop (and the shell) instead of looping forever re-running it
static int execute_while_or_until(const pen_ast_node * node, pen_tok_list * tok_list, history * hist, pen_alias_table * alias_table, int negate) {
    const pen_ast_node * cond = node->nodes_children[0];
    const pen_ast_node * body = node->nodes_children[1];
    int status = 0;   // POSIX: 0 if the body never runs

    for (;;) {
        int cond_status = execute_compound_list_status(cond, tok_list, hist, alias_table);
        int should_run = negate ? (cond_status != 0) : (cond_status == 0);
        if (!should_run) break;

        status = execute_compound_list_status(body, tok_list, hist, alias_table);
        if (pen_should_exit) break;
    }

    pen_last_status = status;
    return status;
}

// runs a FOR_CLAUSE node: node->tok is the loop variable name, nodes_children = [wordlist, body].
// Each wordlist item is exported via setenv so the existing ${VAR} expansion machinery
// (expand_vars_in_str, via getenv) picks it up in the body with no separate "shell variable"
// concept needed -- matches how this shell already treats xpt/PWD/etc. as real env vars.
static int execute_for_clause(const pen_ast_node * node, pen_tok_list * tok_list, history * hist, pen_alias_table * alias_table) {
    const char * var_name = node->tok->text;
    const pen_ast_node * body = node->nodes_children[1];
    int status = 0;   // POSIX: 0 if the body never runs

    const pen_ast_node * cur = node->nodes_children[0];
    while (cur->child_count > 0) {
        char * value = flatten_arg(cur->nodes_children[0]);
        setenv(var_name, value, 1);
        free(value);

        status = execute_compound_list_status(body, tok_list, hist, alias_table);
        if (pen_should_exit) break;

        cur = cur->nodes_children[1];
    }

    pen_last_status = status;
    return status;
}

// dispatches a PIPELINE's compound-command child (see generate_pipeline_node) to its executor
static int execute_compound_command(const pen_ast_node * node, pen_tok_list * tok_list, history * hist, pen_alias_table * alias_table) {
    switch (node->node_type) {
        case IF_CLAUSE:    return execute_if_clause(node, tok_list, hist, alias_table);
        case WHILE_CLAUSE: return execute_while_or_until(node, tok_list, hist, alias_table, 0);
        case UNTIL_CLAUSE: return execute_while_or_until(node, tok_list, hist, alias_table, 1);
        case FOR_CLAUSE:   return execute_for_clause(node, tok_list, hist, alias_table);
        default:           return 1;   // unreachable -- generate_pipeline_node never produces another type here
    }
}

//runs a <pipeline>: dispatches its pipe_sequence or compound command, then applies '!' negation;
//returns the (possibly negated) exit status.
//updates pen_last_status here (not just once per line) so $?/${?} in a later command -- even one on the
//same line, or later in the same and_or chain -- see the freshest value, matching real shell semantics
static int execute_pipeline(const pen_ast_node * pipeline, pen_tok_list * tok_list, history * hist, pen_alias_table * alias_table) {
    const pen_ast_node * stage = pipeline->nodes_children[0];

    int status;
    if (stage->node_type == PIPE_SEQUENCE) {
        size_t stages = pipe_sequence_stage_count(stage);
        status = (stages == 1)
            ? dispatch_command(stage->nodes_children[0], tok_list, hist, alias_table)
            : execute_pipe_sequence(stage, stages);
    } else {
        status = execute_compound_command(stage, tok_list, hist, alias_table);
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

// Appends every token from `addition` onto `base`, first dropping base's own
// trailing CONTINUATION marker (the '\' that asked for more input) so it
// doesn't survive into the assembled line. Takes ownership of addition's
// token text and frees addition's own containers; rebuilds base->args to
// match the merged token array.
static void append_continuation(pen_tok_list * base, pen_tok_list * addition) {
    size_t keep = base->n;
    if (keep > 0 && base->toks[keep - 1].tok_type == CONTINUATION) {
        free(base->toks[keep - 1].text);
        keep--;
    }

    base->toks = realloc(base->toks, sizeof(pen_tok) * (keep + addition->n));
    for (size_t i = 0; i < addition->n; i++) {
        base->toks[keep + i] = addition->toks[i];    // ownership of text moves to base
    }
    base->n = keep + addition->n;

    free(addition->toks);
    free(addition->args);
    free(addition);

    free(base->args);
    base->args = malloc(sizeof(char *) * (base->n + 1));
    for (size_t i = 0; i < base->n; i++) base->args[i] = base->toks[i].text;
    base->args[base->n] = NULL;
}

// Space-joins a tok_list's token text for history display. Used only for a
// line assembled from multiple continuation fragments, where (unlike the
// common single-fragment case) no single raw `cmmd` string spans the whole
// thing -- each fragment was read on its own readline() call.
static char * join_tok_text(const pen_tok_list * tok_list) {
    size_t len = 1;
    for (size_t i = 0; i < tok_list->n; i++) len += strlen(tok_list->toks[i].text) + 1;

    char * out = malloc(len);
    out[0] = '\0';
    for (size_t i = 0; i < tok_list->n; i++) {
        strcat(out, tok_list->toks[i].text);
        if (i + 1 < tok_list->n) strcat(out, " ");
    }
    return out;
}

// Processes one logical command line, reading and folding in further input
// whenever the line ends with a lone '\' continuation marker (see the
// CONTINUATION token and generate_line_node). `pending_tok_list` carries the
// tokens assembled from earlier fragments of the same logical line -- NULL
// for a fresh, non-continued call; on a continuation, process_line calls
// itself with the freshly-read next line and the accumulated tok_list so far.
static void process_line(char * cmmd, history * hist, pen_alias_table * alias_table, int is_pen_rc, pen_tok_list * pending_tok_list) {

    pen_tok_list * tok_list = tokenize(cmmd, strlen(cmmd));

    if (pending_tok_list != NULL) {
        append_continuation(pending_tok_list, tok_list);
        tok_list = pending_tok_list;
    }

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

    pen_ast_node * line = parse(expanded_tok_list_with_vars);
    if (line == NULL) {                     // "| ls", "ls |", or a token the grammar can't take yet
        if(debug_mode_flag) fprintf(stderr, "penguin: syntax error\n");
        free_tokens(tok_list);
        free_tokens(expanded_tok_list);
        free_tokens(expanded_tok_list_with_vars);
        return;
    }

    // a trailing lone '\' means this logical line isn't finished yet -- read
    // more input and fold it into tok_list rather than running anything now
    if (tok_list->n > 0 && tok_list->toks[tok_list->n - 1].tok_type == CONTINUATION) {
        free_ast(line);
        if(expanded_tok_list != NULL) free_tokens(expanded_tok_list);
        free_tokens(expanded_tok_list_with_vars);

        char * more = readline(getenv("PS2"));
        if (more == NULL) {                 // EOF/Ctrl-D mid continuation: abandon the line
            free_tokens(tok_list);
            return;
        }
        process_line(more, hist, alias_table, is_pen_rc, tok_list);
        free(more);
        return;
    }

    //record every non-empty line, the way a shell keeps everything you typed
    //we record the original input pre variable resolution
    if(is_pen_rc != 1) {
        if (pending_tok_list != NULL) {     // assembled from continuation fragments -- no single raw cmmd spans all of them
            char * full_cmmd = join_tok_text(tok_list);
            record_history(hist, full_cmmd, tok_list, tok_list->n);
            free(full_cmmd);
        } else {
            record_history(hist, cmmd, tok_list, tok_list->n);
        }
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

                    process_line(line, hist, alias_table, 1, NULL);
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

// Substitutes the (single) PS1_CWD_TOKEN placeholder in ps1_template with the
// live cwd -- decoupled from PWD/cd this way, PS1 is just a template and never
// needs updating when the working directory changes; only the render needs
// the current directory. A template without the token is copied through as-is.
static void resolve_ps1(const char * ps1_template, const char * cwd, char * out, size_t out_size) {
    const char * token = strstr(ps1_template, PS1_CWD_TOKEN);
    if (token == NULL) {
        snprintf(out, out_size, "%s", ps1_template);
        return;
    }
    int prefix_len = (int)(token - ps1_template);
    snprintf(out, out_size, "%.*s%s%s", prefix_len, ps1_template, cwd, token + strlen(PS1_CWD_TOKEN));
}

static char * build_prompt(char * prompt) {

    char cwd[MAX_PATH_LEN] = {0};

    //get the current working directory
    getcwd(cwd, MAX_PATH_LEN);

    char * git_branch = get_git_branch(cwd);

    //build the git branch segment, colored, only if we're inside a repo
    char git_segment[128] = {0};
    if(git_branch != NULL){
        snprintf(git_segment, sizeof(git_segment), "\001\033[38;2;133;50;168m\002(%.32s)\001\033[38;2;0;255;255m\002 ", git_branch);
    }

    //fetch the base prompt from PS1 and resolve its cwd placeholder against the live cwd
    char * ps1_template = getenv("PS1");
    if(ps1_template == NULL){
        ps1_template = "";
    }

    char ps1[PROMPT_BUF_LEN];
    resolve_ps1(ps1_template, cwd, ps1, sizeof(ps1));

    snprintf(prompt, PROMPT_BUF_LEN, "%s%s" "\001\033[0m\002", ps1, git_segment);

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
                process_line("alias lla=\"ls -la\"", hist, alias_table, 1, NULL);
                process_line("alias ll=\"ls -ll\"", hist, alias_table, 1, NULL);
                process_line("xpt PEN_HOME=${HOME}/Penguin-Shell", hist, alias_table, 1, NULL);
                break;
            default:
                fprintf(stdout, "%s", USAGE);
                exit(1);
        }
    }
}

static void set_penguin_boot_vars(history * hist, pen_alias_table * alias_table){

    //Set the ENV variable
    process_line("xpt ENV=\"${HOME}/.penrc\"", hist, alias_table, 1, NULL);

    //Set the HOME variable
    uid_t uid = getuid();
    struct passwd * pwd = getpwuid(uid);
    if(pwd != NULL){
        char home_cmd[MAX_PATH_LEN + 16];
        snprintf(home_cmd, sizeof(home_cmd), "xpt HOME=%s", pwd->pw_dir);
        process_line(home_cmd, hist, alias_table, 1, NULL);
    }else{
        printf("Error setting HOME! errno: %d\n", errno);
    }

    //Set the IFS variable
    process_line("xpt IFS=\" \t\n\"", hist, alias_table, 1, NULL);

    //Set the LANG variable
    process_line("xpt LANG=\"C.UTF-8\"", hist, alias_table, 1, NULL);

    //Set the LC_ALL variable
    process_line("xpt LC_ALL=\"C.UTF-8\"", hist, alias_table, 1, NULL);

    //Set the LC_COLLATE variable
    process_line("xpt LC_COLLATE=\"en_US.UTF-8\"", hist, alias_table, 1, NULL);

    //Set the LC_CTYPE variable
    process_line("xpt LC_COLLATE=\"POSIX\"", hist, alias_table, 1, NULL);

    //Set the LC_MESSAGES variable
    process_line("xpt LC_MESSAGES=\"en_US.UTF-8\"", hist, alias_table, 1, NULL);

    //Set the LINENO variable
    process_line("xpt LINENO=1", hist, alias_table, 1, NULL);

    //Set the NLSPATH variable
    process_line("xpt NLSPATH=/usr/share/nls/%L/%N.cat:/usr/share/nls/default/%N.cat", hist, alias_table, 1, NULL);

    //Set the PATH variable
    process_line("xpt PATH=\"/home/nate/Penguin-Shell/build:/home/nate/pychar-2023.2.5/bin:/home/nate/GoLand-2025.1.3/bin:/home/nate/clion/bin:${PATH}\"", hist, alias_table, 1, NULL);

    //Set the PPID variable
    char ppid_cmd[32];
    snprintf(ppid_cmd, sizeof(ppid_cmd), "xpt PPID=%d", getppid());
    process_line(ppid_cmd, hist, alias_table, 1, NULL);

    //Set the PS1 variable. Uses the PS1_CWD_TOKEN placeholder rather than a
    //snapshot of the boot-time cwd -- build_prompt resolves it against the
    //live cwd on every render, so cd never needs to touch PS1.
    char ps1_host[MAX_PATH_LEN] = {0};
    gethostname(ps1_host, sizeof(ps1_host));

    if(pwd != NULL){
        char ps1_cmd[MAX_PATH_LEN + 256];
        snprintf(ps1_cmd, sizeof(ps1_cmd), "xpt PS1=\"\001\033[38;2;0;255;255m\002%.32s@%.32s#" PS1_CWD_TOKEN " (•ᴗ•)ゝ \"", pwd->pw_name, ps1_host);
        process_line(ps1_cmd, hist, alias_table, 1, NULL);
    }

    //Set the PS2 variable
    process_line("xpt PS2=\"(•ᴗ•)ゝ -> \"", hist, alias_table, 1, NULL);

    //Set the PS4 variable
    process_line("xpt PS4=\"(•ᴗ•)ゝ [Line: $LINENO] -> \"", hist, alias_table, 1, NULL);

    //Set the PWD variable
    char pwd_cwd[MAX_PATH_LEN] = {0};
    if (getcwd(pwd_cwd, MAX_PATH_LEN) != NULL) {
        char pwd_cmd[MAX_PATH_LEN + 16];
        snprintf(pwd_cmd, sizeof(pwd_cmd), "xpt PWD=%s", pwd_cwd);
        process_line(pwd_cmd, hist, alias_table, 1, NULL);
    }
}

//END SECTION: Main shell loop commands

//SECTION: Main method
//main method that runs the shell, handles the main REPL loop and calls the appropriate methods to execute the commands entered by the user
int run(int argc, char ** argv, history * hist, pen_alias_table * alias_table) {

    //set the POSIX vars
    set_penguin_boot_vars(hist, alias_table);

    //Initialize git tools
    git_libgit2_init();

    parse_options(argc, argv, hist, alias_table);

    process_rc(hist, alias_table);

    pen_greet(NULL, NULL, NULL, 0);

    //main REPL loop
    char * cmmd;
    char prompt[PROMPT_BUF_LEN];

    while (!pen_should_exit && (cmmd = readline(build_prompt(prompt))) != NULL) {
        process_line(cmmd, hist, alias_table, 0, NULL);
        free(cmmd);
    }

    return 0;
}
//END SECTION: Main method