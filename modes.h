#ifndef MODES_H
#define MODES_H

#include "term.h"

void normal_mode_keypress(enum term_key_type type, unsigned int key);
void command_mode_keypress(enum term_key_type type, unsigned int key);
void search_mode_keypress(enum term_key_type type, unsigned int key);

struct editor_mode_ops {
	void (*keypress)(enum term_key_type type, unsigned int key);
};

extern struct editor_mode_ops modes[];

#endif
