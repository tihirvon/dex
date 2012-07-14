#ifndef SCREEN_H
#define SCREEN_H

#include "window.h"

void print_tabbar(void);
int print_command(char prefix);
void print_message(const char *msg, int is_error);
void update_term_title(void);
void update_range(int y1, int y2);
void update_separators(void);
void update_status_line(void);
void update_window_sizes(void);
void update_line_numbers(struct window *win, int force);
void update_git_open(void);
void update_screen_size(void);

#endif
