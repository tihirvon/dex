#ifndef EDIT_H
#define EDIT_H

struct block_iter;

unsigned int prepare_selection(void);

void delete(unsigned int len, int move_after);
void cut(unsigned int len, int is_lines);
void copy(unsigned int len, int is_lines);
void select_end(void);
void paste(void);
void join_lines(void);
void shift_lines(int count);
void clear_lines(void);
void new_line(void);
void format_paragraph(int pw);

#endif
