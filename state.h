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
	COND_LISTED_HASH,
	COND_NOEAT,
	COND_RECOLOR,
	COND_STR,
	COND_STR2,
	COND_STR_ICASE,
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
			int len;
		} cond_recolor;
		struct {
			char *str;
			int len;
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
	int icase;
	int hash;
};

struct syntax {
	struct list_head node;
	char *name;
	struct ptr_array states;
	struct ptr_array string_lists;
	int subsyntax;
};

unsigned int buf_hash(const char *str, unsigned int size);
struct syntax *load_syntax_file(const char *filename, int must_exist);
struct syntax *find_syntax(const char *name);
void update_syntax_colors(struct syntax *syn);
void update_all_syntax_colors(void);

#endif
