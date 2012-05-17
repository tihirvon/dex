#ifndef EDITOR_H
#define EDITOR_H

#include "libc.h"

enum editor_status {
	EDITOR_INITIALIZING,
	EDITOR_RUNNING,
	EDITOR_EXITING,
};

enum input_mode {
	INPUT_NORMAL,
	INPUT_COMMAND,
	INPUT_SEARCH,
};

enum input_special {
	/* not inputting special characters */
	INPUT_SPECIAL_NONE,

	/* not known yet (just started by hitting ^V) */
	INPUT_SPECIAL_UNKNOWN,

	/* accept any value 0-255 (3 octal digits) */
	INPUT_SPECIAL_OCT,

	/* accept any value 0-255 (3 decimal digits) */
	INPUT_SPECIAL_DEC,

	/* accept any value 0-255 (2 hexadecimal digits) */
	INPUT_SPECIAL_HEX,

	/* accept any valid unicode value (6 hexadecimal digits) */
	INPUT_SPECIAL_UNICODE,
};

extern enum editor_status editor_status;
extern enum input_mode input_mode;
extern enum input_special input_special;
extern struct cmdline cmdline;
extern char *home_dir;
extern char *charset;
extern int child_controls_terminal;
extern int resized;
extern int nr_errors;

extern const char *program;
extern const char *version;
extern const char *pkgdatadir;

char *editor_file(const char *name);
char get_confirmation(const char *choices, const char *format, ...) FORMAT(2, 3);
void discard_paste(void);
void any_key(void);
void resize(void);
void ui_end(void);
void suspend(void);
void set_signal_handler(int signum, void (*handler)(int));
void main_loop(void);

#endif
