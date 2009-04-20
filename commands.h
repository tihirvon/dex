#ifndef COMMANDS_H
#define COMMANDS_H

#include "term.h"

struct command {
	const char *name;
	const char *short_name;
	void (*cmd)(char **);
};

struct parsed_command {
	// can contain many commands. each terminated with NULL
	char **argv;

	// includes NULLs
	int count;
	int alloc;
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
int parse_commands(struct parsed_command *pc, const char *cmd);
void free_commands(struct parsed_command *pc);
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
