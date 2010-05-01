#ifndef COMMANDS_H
#define COMMANDS_H

#include "ptr-array.h"

struct command {
	const char *name;
	const char *flags;
	signed char min_args;
	signed char max_args;
	void (*cmd)(const char *, char **);
};

extern const struct command commands[];

char *parse_command_arg(const char *cmd, int tilde);
int find_end(const char *cmd, int *posp);
int parse_commands(struct ptr_array *array, const char *cmd);

#endif
