#include "pen-main/penguin-main.h"

//method that handles cleaning up the resources taken by the shell before exiting, such as the history and alias table
static int clean_up(history * hist, pen_alias_table * alias_table) {
    clean_history(hist);
    clear_alias_table(alias_table);
    rl_clear_history();
    rl_free_line_state();
    return 0;
}

int main(const int argc, char ** argv) {

    //initialize the history and alias table
    history * hist = init_history();
    pen_alias_table * alias_table = init_alias_table();

    run(argc, argv, hist, alias_table);
    clean_up(hist, alias_table);
    return 0;
}