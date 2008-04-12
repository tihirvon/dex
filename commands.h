#ifndef COMMANDS_H
#define COMMANDS_H

#include "term.h"

struct parsed_command {
	// can contain many commands. each terminated with NULL
	char **argv;

	// includes NULLs
	int count;
	int alloc;
};

int parse_commands(struct parsed_command *pc, const char *cmd, int cursor_pos);
void free_commands(struct parsed_command *pc);
void handle_command(const char *cmd);
void handle_binding(enum term_key_type type, unsigned int key);
void read_config(void);

#endif
