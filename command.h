#ifndef COMMAND_H
#define COMMAND_H

#include "ptr-array.h"

struct command {
	const char *name;
	const char *flags;
	signed char min_args;
	signed char max_args;
	void (*cmd)(const char *, char **);
};

/* parse-command.c */
char *parse_command_arg(const char *cmd, int tilde);
int find_end(const char *cmd, int *posp);
int parse_commands(struct ptr_array *array, const char *cmd);

/* run.c */
extern const struct command *current_command;

const struct command *find_command(const struct command *cmds, const char *name);
void run_commands(const struct command *cmds, const struct ptr_array *array);
void run_command(const struct command *cmds, char **av);
void handle_command(const struct command *cmds, const char *cmd);

/* commands.c */
extern const struct command commands[];

#endif
