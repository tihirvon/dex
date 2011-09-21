#ifndef STATE_H
#define STATE_H

#include "ptr-array.h"

enum condition_type {
	COND_BUFIS,
	COND_CHAR,
	COND_CHAR_BUFFER,
	COND_INLIST,
	COND_INLIST_HASH,
	COND_RECOLOR,
	COND_RECOLOR_BUFFER,
	COND_STR,
	COND_STR2,
	COND_STR_ICASE,
};

struct action {
	union {
		char *name;			// set while parsing syntax file
		struct state *state;		// set after parsed syntax file
	} destination;

	// If condition has no emit name this is set to destination state's
	// emit name or list name (COND_LIST).
	char *emit_name;

	// Set after all colors have been added (config loaded).
	struct hl_color *emit_color;
};

struct condition {
	union {
		struct {
			int len;
			int icase;
			char str[256 / 8 - 2 * sizeof(int)];
		} cond_bufis;
		struct {
			unsigned char bitmap[256 / 8];
		} cond_char;
		union {
			char *list_name;		// set while parsing syntax file
			struct string_list *list;	// set after parsed syntax file
		} cond_inlist;
		struct {
			int len;
		} cond_recolor;
		struct {
			int len;
			char str[256 / 8 - sizeof(int)];
		} cond_str;
	} u;
	struct action a;
	enum condition_type type;
};

struct state {
	char *name;
	char *emit_name;
	struct ptr_array conds;
	int visited;

	struct action a;
	int noeat;
};

struct hash_str {
	struct hash_str *next;
	int len;
	char str[1];
};

struct string_list {
	char *name;
	union {
		char **strings;
		struct hash_str *hash[62];
	} u;
	unsigned int icase : 1;
	unsigned int hash : 1;
	unsigned int used : 1;
};

struct syntax {
	char *name;
	struct ptr_array states;
	struct ptr_array string_lists;
	struct ptr_array default_colors;
	int subsyntax;
};

unsigned int buf_hash(const char *str, unsigned int size);
struct syntax *load_syntax_file(const char *filename, int must_exist);
struct syntax *find_syntax(const char *name);
void update_syntax_colors(struct syntax *syn);
void update_all_syntax_colors(void);

#endif
