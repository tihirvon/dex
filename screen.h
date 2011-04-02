#ifndef SCREEN_H
#define SCREEN_H

void set_basic_colors(void);
void print_tabbar(void);
int print_command(char prefix);
void print_message(const char *msg, int is_error);
void print_term_title(const char *title);
void update_range(int y1, int y2);
void update_status_line(const char *misc_status);
void update_window_sizes(void);
void update_line_numbers(int force);
void update_screen_size(void);

#endif
