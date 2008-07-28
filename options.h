#ifndef OPTIONS_H
#define OPTIONS_H

enum newline_sequence {
	NEWLINE_UNIX,
	NEWLINE_DOS,
};

struct local_options {
	int auto_indent;
	int expand_tab;
	int indent_width;
	int tab_width;
	int trim_whitespace;
};

struct global_options {
	int auto_indent;
	int expand_tab;
	int indent_width;
	int tab_width;
	int trim_whitespace;

	int allow_incomplete_last_line;
	int move_wraps;

	// this is just the default for new files
	enum newline_sequence newline;

	char *statusline_left;
	char *statusline_right;
};

extern struct global_options options;

#define OPT_LOCAL	(1 << 0)
#define OPT_GLOBAL	(1 << 1)

void set_option(const char *name, const char *value, unsigned int flags);
void toggle_option(const char *name, unsigned int flags);
void collect_options(const char *prefix);
void collect_option_values(const char *name, const char *prefix);
void init_options(void);

#endif
