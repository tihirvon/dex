#ifndef OPTIONS_H
#define OPTIONS_H

enum newline_sequence {
	NEWLINE_UNIX,
	NEWLINE_DOS,
};

struct options {
	int allow_incomplete_last_line;
	int auto_indent;
	int expand_tab;
	int indent_width;
	int move_wraps;
	int tab_width;
	int trim_whitespace;

	// this is just the default for new files
	enum newline_sequence newline;
};

extern struct options options;

void set_option(const char *name, const char *value);
void collect_options(const char *prefix);
void collect_option_values(const char *name, const char *prefix);

#endif
