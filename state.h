#ifndef STATE_H
#define STATE_H

#include "list.h"
#include "ptr-array.h"
#include "command.h"

enum condition_type {
	COND_BUFFER,
	COND_BUFIS,
	COND_CHAR,
	COND_CHAR_BUFFER,
	COND_EAT,
	COND_LISTED,
	COND_NOEAT,
	COND_STR,
};

struct condition {
	union {
		struct {
			char *str;
			int len;
			int icase;
		} cond_bufis;
		struct {
			unsigned char bitmap[256 / 8];
		} cond_char;
		union {
			char *list_name;		// set while parsing syntax file
			struct string_list *list;	// set after parsed syntax file
		} cond_listed;
		struct {
			char *str;
			int len;
			int icase;
		} cond_str;
	} u;
	union {
		char *name;			// set while parsing syntax file
		struct state *state;		// set after parsed syntax file
	} destination;

	// If condition has no emit name this is set to destination state's
	// emit name or list name (COND_LIST).
	char *emit_name;

	// Set after all colors have been added (config loaded).
	struct hl_color *emit_color;

	enum condition_type type;
};

struct state {
	char *name;
	char *emit_name;
	struct condition *conditions;
	int nr_conditions;
	int visited;
};

struct string_list {
	char *name;
	char **strings;
	int icase;
};

struct syntax {
	struct list_head node;
	char *name;
	struct ptr_array states;
	struct ptr_array string_lists;
};

struct syntax *load_syntax_file(const char *filename, const char *name);
struct syntax *find_syntax(const char *name);
void update_syntax_colors(struct syntax *syn);
void update_all_syntax_colors(void);

#endif
