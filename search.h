#ifndef SEARCH_H
#define SEARCH_H

enum search_direction {
	SEARCH_FWD,
	SEARCH_BWD,
};

extern enum search_direction search_direction;

void search_init(enum search_direction dir);
void search(const char *pattern);
void search_prev(void);
void search_next(void);
void reg_replace(const char *pattern, const char *format, const char *flags_str);

#endif
