#ifndef COMMANDS_H
#define COMMANDS_H

#include "term.h"

char **parse_commands(const char *cmd, int *argcp);
void handle_command(const char *cmd);
void handle_binding(enum term_key_type type, unsigned int key);
void read_config(void);

#endif
