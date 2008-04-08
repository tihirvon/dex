#ifndef CMDLINE_H
#define CMDLINE_H

#include "uchar.h"
#include "gbuf.h"

extern struct gbuf cmdline;
extern int cmdline_pos;

void cmdline_insert(uchar u);
void cmdline_delete(void);
void cmdline_backspace(void);
void cmdline_delete_bol(void);
void cmdline_prev_char(void);
void cmdline_next_char(void);
void cmdline_clear(void);
void cmdline_set_text(const char *text);

#endif
