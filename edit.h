#ifndef EDIT_H
#define EDIT_H

unsigned int prepare_selection(void);

void select_block(void);
void unselect(void);
void cut(unsigned int len, int is_lines);
void copy(unsigned int len, int is_lines);
void insert_text(const char *text, unsigned int size);
void paste(void);
void delete_ch(void);
void erase(void);
void insert_ch(unsigned int ch);
void join_lines(void);
void shift_lines(int count);
void clear_lines(void);
void new_line(void);
void format_paragraph(int pw);
void change_case(int mode, int move_after);

#endif
