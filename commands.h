#ifndef COMMANDS_H
#define COMMANDS_H

#include "term.h"
#include "ptr-array.h"

struct command {
	const char *name;
	const char *short_name;
	void (*cmd)(char **);
};

struct alias {
	char *name;
	char *value;
};

extern const struct command commands[];
extern const struct command *current_command;

extern const char *config_file;
extern int config_line;

extern struct alias *aliases;
extern unsigned int alias_count;

char *parse_command_arg(const char *cmd, int tilde);
int find_end(const char *cmd, int *posp);
int parse_commands(struct ptr_array *array, const char *cmd);
void handle_command(const char *cmd);
void handle_binding(enum term_key_type type, unsigned int key);
int read_config(const char *filename, int must_exist);

const char *parse_args(char **args, const char *flags, int min, int max);
const struct command *find_command(const struct command *cmds, const char *name);
void complete_command(void);
void reset_completion(void);
void add_completion(char *str);
void sort_aliases(void);
void set_file_options(void);

#endif
