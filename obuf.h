#ifndef OBUF_H
#define OBUF_H

#include "uchar.h"

struct output_buffer {
	unsigned char *buf;
	unsigned int alloc;
	unsigned int count;

	// number of characters scrolled (x direction)
	unsigned int scroll_x;

	// current x position (tab 1-8, double-width 2, invalid utf8 byte 4)
	// if smaller than scroll_x printed characters are not visible
	unsigned int x;

	// width of screen
	unsigned int width;

	unsigned int tab_width;

	int bg;
};

extern struct output_buffer obuf;

void buf_add_bytes(const char *str, int count);
void buf_set_bytes(char ch, int count);
void buf_ch(char ch);
void buf_escape(const char *str);
void buf_hide_cursor(void);
void buf_show_cursor(void);
void buf_move_cursor(int x, int y);
void buf_set_colors(int fg, int bg);
void buf_clear_eol(void);
void buf_flush(void);
void buf_skip(uchar u, int utf8);
int buf_put_char(uchar u, int utf8);

#endif
