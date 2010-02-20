#ifndef SCREEN_H
#define SCREEN_H

extern int screen_w;
extern int screen_h;

extern int nr_errors;
extern int msg_is_error;
extern char error_buf[256];

void print_tab_bar(void);
void update_range(int y1, int y2);
void update_full(void);
void update_status_line(void);
void update_command_line(void);
void restore_cursor(void);
void update_window_sizes(void);
void update_screen_size(void);
void update_everything(void);
void set_basic_colors(void);

#endif
