#ifndef MOVE_H
#define MOVE_H

struct block_iter;

int get_indent_level_bytes_left(void);
int get_indent_level_bytes_right(void);

void move_to_preferred_x(void);
void move_left(int count);
void move_right(int count);
void move_cursor_left(void);
void move_cursor_right(void);
void move_bol(void);
void move_eol(void);
void move_up(int count);
void move_down(int count);
void move_bof(void);
void move_eof(void);
void move_to_line(int line);
void move_to_column(int column);

unsigned int word_fwd(struct block_iter *bi);
unsigned int word_bwd(struct block_iter *bi);

#endif
