#ifndef COMMANDS_H
#define COMMANDS_H

#include "ptr-array.h"

struct command {
	const char *name;
	void (*cmd)(char **);
};

extern const struct command commands[];
extern const struct command *current_command;

extern const char *config_file;
extern int config_line;

char *parse_command_arg(const char *cmd, int tilde);
int find_end(const char *cmd, int *posp);
int parse_commands(struct ptr_array *array, const char *cmd);
void run_commands(const struct ptr_array *array);
void handle_command(const char *cmd);
int read_config(const char *filename, int must_exist);

const char *parse_args(char **args, const char *flags, int min, int max);
const struct command *find_command(const struct command *cmds, const char *name);
void complete_command(void);
void reset_completion(void);
void add_completion(char *str);
void set_file_options(void);

#endif
