#ifndef SCREEN_H
#define SCREEN_H

void print_tab_bar(void);
void print_command(char prefix);
void print_message(const char *msg, int is_error);
void update_range(int y1, int y2);
void update_status_line(const char *misc_status);
void restore_cursor(void);
void update_window_sizes(void);
void update_screen_size(void);
void set_basic_colors(void);

#endif
