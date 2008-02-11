#include "term.h"
#include "xmalloc.h"

#include <sys/ioctl.h>
#include <unistd.h>
#include <termios.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

char *term_keycodes[NR_SKEYS];
struct term_cap term_cap;
unsigned int term_flags;

static struct termios termios_save;
static char buffer[64];
static int buffer_pos;

static void buffer_num(unsigned int num)
{
	char stack[32];
	int ret, pos = 0;

	do {
		stack[pos++] = (num % 10) + '0';
		num /= 10;
	} while (num);
	ret = pos;
	do {
		buffer[buffer_pos++] = stack[--pos];
	} while (pos);
}

int term_init(const char *term, unsigned int flags)
{
	char filename[512];
	int rc;

	term_flags = flags;

	if (term == NULL)
		term = getenv("TERM");
	if (term == NULL || term[0] == 0)
		term = "linux";

	rc = -2;
	if (flags & TERM_USE_TERMINFO) {
		/*
		 * /etc/terminfo?
		 */
		const char *path = getenv("TERMINFO");

		if (path == NULL || path[0] == 0)
			path = "/usr/share/terminfo";
		snprintf(filename, sizeof(filename), "%s/%c/%s", path, term[0], term);
		rc = terminfo_get_caps(filename);
	}

	if (rc && flags & TERM_USE_TERMCAP) {
		rc = termcap_get_caps("/etc/termcap", term);
	}
	return rc;
}

void term_raw(void)
{
	/* see termios(3) */
	struct termios termios;

	tcgetattr(0, &termios);
	termios_save = termios;

	/* disable buffering and echo */
	termios.c_lflag &= ~(ICANON | ECHO);

	/* disable CR to NL conversion (differentiate ^J from enter)
	 * disable flow control (free ^Q and ^S)
	 */
	termios.c_iflag &= ~(ICRNL | IXON | IXOFF);

	/* read at least 1 char on each read() */
	termios.c_cc[VMIN] = 1;

	if (!(term_flags & TERM_ESC_META))
		termios.c_cc[VTIME] = 0;

	tcsetattr(0, 0, &termios);
}

void term_cooked(void)
{
	term_set_attributes(-1, -1, 0);
	tcsetattr(0, 0, &termios_save);
}

static int input_read(char *buf, int buf_size)
{
	int rc;

	rc = read(0, buf, buf_size - 1);
	if (rc == -1)
		rc = 0;
	buf[rc] = 0;
	return rc;
}

static int input_get_byte(char *buf, int buf_size, unsigned char *ch)
{
	int len;

	if (buf[0] == 0) {
		input_read(buf, buf_size);
		if (buf[0] == 0)
			return 0;
	}

	*ch = buf[0];
	len = strlen(buf + 1);
	memmove(buf, buf + 1, len + 1);
	return 1;
}

enum term_key_type term_read_key(unsigned int *key)
{
	const int buf_size = 32;
	static char buf[32 + 1] = { 0, };
	int buf_fill = strlen(buf);
	unsigned char ch;

	/*
	 * One read always returns the whole key sequence if there's
	 * enough space. Therefore we drain the buffer before doing
	 * next read().
	 *
	 * Pasted text may not fit in the buffer but it doesn't matter,
	 * we read it one character at time anyways.
	 *
	 * Ouch, UTF-8 is multibyte.
	 */
	if (buf_fill == 0) {
		buf_fill = input_read(buf, buf_size);
		if (buf_fill == 0) {
			*key = 0;
			return KEY_NONE;
		}
	}

	if (buf_fill > 1) {
		int i;

		for (i = 0; i < NR_SKEYS; i++) {
			if (term_keycodes[i] && strcmp(term_keycodes[i], buf) == 0) {
				*key = i;
				buf[0] = 0;
				return KEY_SPECIAL;
			}
		}
		if (buf_fill == 2 && buf[0] == '\033') {
			/* 'esc key' or 'alt-key' */
			*key = buf[1];
			buf[0] = 0;
			return KEY_META;
		}
	}
	/* > 0 bytes in buf */
	input_get_byte(buf, buf_size, &ch);

	if (ch == 0x7f || ch == 0x08) {
		*key = SKEY_BACKSPACE;
		return KEY_SPECIAL;
	}

	/* normal key */
	if ((term_flags & TERM_UTF8) && ch > 0x7f) {
		/*
		 * 10xx xxxx invalid
		 * 110x xxxx valid
		 * 1110 xxxx valid
		 * 1111 0xxx valid
		 * 1111 1xxx invalid
		 */
		unsigned int u, bit = 1 << 6;
		int count = 0;

		while (ch & bit) {
			bit >>= 1;
			count++;
		}
		if (count == 0 || count > 3) {
			/* invalid first byte */
			*key = 0;
			return KEY_INVALID;
		}
		u = ch & (bit - 1);
		do {
			if (!input_get_byte(buf, buf_size, &ch)) {
				*key = 0;
				return KEY_INVALID;
			}
			if (ch >> 6 != 2) {
				*key = 0;
				return KEY_INVALID;
			}
			u = (u << 6) | (ch & 0x3f);
		} while (--count);
		*key = u;
	} else {
		*key = ch;
	}
	return KEY_NORMAL;
}

int term_get_size(int *w, int *h)
{
	struct winsize ws;

	if (ioctl(0, TIOCGWINSZ, &ws) != -1) {
		*w = ws.ws_col;
		*h = ws.ws_row;
		return 0;
	}
	return -1;
}

static void buffer_color(char x, int color)
{
	if (color < 0)
		return;

	buffer[buffer_pos++] = ';';
	buffer[buffer_pos++] = x;
	if (color < 8) {
		buffer[buffer_pos++] = '0' + color;
	} else {
		buffer[buffer_pos++] = '8';
		buffer[buffer_pos++] = ';';
		buffer[buffer_pos++] = '5';
		buffer[buffer_pos++] = ';';
		buffer_num(color);
	}
}

const char *term_set_attributes(int fg, int bg, unsigned int attr)
{
	if (term_cap.colors <= 16 && fg > 7) {
		attr |= ATTR_BOLD;
		fg &= 7;
	}

	// max 35 bytes (3 + 6 * 2 + 2 * 9 + 2)
	buffer_pos = 0;
	buffer[buffer_pos++] = '\033';
	buffer[buffer_pos++] = '[';
	buffer[buffer_pos++] = '0';

	if (attr & ATTR_BOLD) {
		buffer[buffer_pos++] = ';';
		buffer[buffer_pos++] = '1';
	}
	if (attr & ATTR_LOW_INTENSITY) {
		buffer[buffer_pos++] = ';';
		buffer[buffer_pos++] = '2';
	}
	if (attr & ATTR_UNDERLINE) {
		buffer[buffer_pos++] = ';';
		buffer[buffer_pos++] = '4';
	}
	if (attr & ATTR_BLINKING) {
		buffer[buffer_pos++] = ';';
		buffer[buffer_pos++] = '5';
	}
	if (attr & ATTR_REVERSE_VIDEO) {
		buffer[buffer_pos++] = ';';
		buffer[buffer_pos++] = '7';
	}
	if (attr & ATTR_INVISIBLE_TEXT) {
		buffer[buffer_pos++] = ';';
		buffer[buffer_pos++] = '8';
	}
	buffer_color('3', fg);
	buffer_color('4', bg);
	buffer[buffer_pos++] = 'm';
	buffer[buffer_pos++] = 0;
	return buffer;
}

const char *term_set_colors(int fg, int bg)
{
	return term_set_attributes(fg, bg, 0);
}

const char *term_move_cursor(int x, int y)
{
	if (x < 0 || x >= 999 || y < 0 || y >= 999)
		return "";

	x++;
	y++;
	// max 11 bytes
	buffer_pos = 0;
	buffer[buffer_pos++] = '\033';
	buffer[buffer_pos++] = '[';
	buffer_num(y);
	buffer[buffer_pos++] = ';';
	buffer_num(x);
	buffer[buffer_pos++] = 'H';
	buffer[buffer_pos++] = 0;
	return buffer;
}
