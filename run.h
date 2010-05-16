#ifndef RUN_H
#define RUN_H

#include "ptr-array.h"

extern const struct command *current_command;

const struct command *find_command(const struct command *cmds, const char *name);
void run_commands(const struct command *cmds, const struct ptr_array *array);
void run_command(const struct command *cmds, char **av);
void handle_command(const struct command *cmds, const char *cmd);

#endif
