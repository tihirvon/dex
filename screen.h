#ifndef SCREEN_H
#define SCREEN_H

#include "window.h"
#include "color.h"

void set_color(struct term_color *color);
void set_builtin_color(enum builtin_color c);
void mask_color(struct term_color *color, const struct term_color *over);

void print_tabbar(void);
int print_command(char prefix);
void print_message(const char *msg, bool is_error);
void update_term_title(void);
void update_range(int y1, int y2);
void update_separators(void);
void update_status_line(void);
void update_window_sizes(void);
void update_line_numbers(struct window *win, bool force);
void update_git_open(void);
void update_screen_size(void);

#endif
