#ifndef SEARCH_H
#define SEARCH_H

enum search_direction {
	SEARCH_FWD,
	SEARCH_BWD,
};

enum {
	REPLACE_CONFIRM = (1 << 0),
	REPLACE_GLOBAL = (1 << 1),
	REPLACE_IGNORE_CASE = (1 << 2),
	REPLACE_BASIC = (1 << 3),
	REPLACE_CANCEL = (1 << 4),
};

void search_tag(const char *pattern);

void search_set_direction(enum search_direction dir);
enum search_direction current_search_direction(void);
void search_set_regexp(const char *pattern);
void search_prev(void);
void search_next(void);

void reg_replace(const char *pattern, const char *format, unsigned int flags);

#endif
