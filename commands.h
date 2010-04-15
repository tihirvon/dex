#ifndef COMMANDS_H
#define COMMANDS_H

#include "ptr-array.h"

struct command {
	const char *name;
	void (*cmd)(char **);
};

extern const struct command commands[];

char *parse_command_arg(const char *cmd, int tilde);
int find_end(const char *cmd, int *posp);
int parse_commands(struct ptr_array *array, const char *cmd);

const char *parse_args(char **args, const char *flags, int min, int max);
void set_file_options(void);

#endif
