#ifndef CONFIG_H
#define CONFIG_H

#include "command.h"

extern const char *config_file;
extern int config_line;

int do_read_config(const struct command *cmds, const char *filename, int must_exist);
int read_config(const struct command *cmds, const char *filename, int must_exist);

#endif
