#ifndef TERM_H
#define TERM_H

#include "libc.h"

enum term_key_type {
	/* key is character encoded in the current locale's encoding */
	KEY_NORMAL,

	/* same as KEY_NORMAL but with Alt pressed or ESC and KEY_NORMAL */
	KEY_META,

	/* key is one of SKEY_* */
	KEY_SPECIAL,

	/* call term_get_paste() to read pasted text */
	KEY_PASTE,
};

enum {
	STR_CAP_CMD_ac, // pairs of block graphic characters to map alternate character set
	STR_CAP_CMD_ae, // end alternative character set
	STR_CAP_CMD_as, // start alternative character set for block graphic characters
	STR_CAP_CMD_ce, // clear to end of line
	STR_CAP_CMD_ke, // turn keypad off
	STR_CAP_CMD_ks, // turn keypad on
	STR_CAP_CMD_te, // end program that uses cursor motion
	STR_CAP_CMD_ti, // begin program that uses cursor motion
	STR_CAP_CMD_ve, // show cursor
	STR_CAP_CMD_vi, // hide cursor

	NR_STR_CAPS
};

enum {
	// keys from terminfo
	SKEY_INSERT,
	SKEY_DELETE,
	SKEY_HOME,
	SKEY_END,
	SKEY_PAGE_UP,
	SKEY_PAGE_DOWN,
	SKEY_LEFT,
	SKEY_RIGHT,
	SKEY_UP,
	SKEY_DOWN,
	SKEY_F1,
	SKEY_F2,
	SKEY_F3,
	SKEY_F4,
	SKEY_F5,
	SKEY_F6,
	SKEY_F7,
	SKEY_F8,
	SKEY_F9,
	SKEY_F10,
	SKEY_F11,
	SKEY_F12,
	SKEY_SHIFT_LEFT,
	SKEY_SHIFT_RIGHT,

	// these are not supported by terminfo
	SKEY_SHIFT_UP,
	SKEY_SHIFT_DOWN,
	SKEY_CTRL_LEFT,
	SKEY_CTRL_RIGHT,
	SKEY_CTRL_UP,
	SKEY_CTRL_DOWN,

	NR_SKEYS
};

#define NR_KEY_CAPS (SKEY_SHIFT_RIGHT + 1)

enum {
	COLOR_DEFAULT = -1,
	COLOR_BLACK,
	COLOR_RED,
	COLOR_GREEN,
	COLOR_YELLOW,
	COLOR_BLUE,
	COLOR_MAGENTA,
	COLOR_CYAN,
	COLOR_GREY
};

enum {
	ATTR_BOLD		= 0x01,
	ATTR_LOW_INTENSITY	= 0x02,
	ATTR_ITALIC		= 0x04,
	ATTR_UNDERLINE		= 0x08,
	ATTR_BLINKING		= 0x10,
	ATTR_REVERSE_VIDEO	= 0x20,
	ATTR_INVISIBLE_TEXT	= 0x40,
	ATTR_KEEP		= 0x80,
};

// see termcap(5)
struct term_cap {
	/* boolean caps */
	bool ut; // can clear to end of line with bg color set

	/* integer caps */
	int colors;

	/* string caps */
	char *strings[NR_STR_CAPS];

	/* string caps (keys) */
	char *keys[NR_KEY_CAPS];
};

struct term_color {
	short fg;
	short bg;
	unsigned short attr;
};

extern struct term_cap term_cap;

// control key
#define CTRL(x) ((x) & ~0x40)

int read_terminfo(const char *term);
void term_setup_extra_keys(const char *term);

void term_raw(void);
void term_cooked(void);

bool term_read_key(unsigned int *key, enum term_key_type *type);
char *term_read_paste(long *size);
void term_discard_paste(void);

int term_get_size(int *w, int *h);

const char *term_set_color(const struct term_color *color);

/* move cursor (x and y are zero based) */
const char *term_move_cursor(int x, int y);

int terminfo_get_caps(const char *filename);

#endif
