#ifndef SPAWN_H
#define SPAWN_H

#include "compiler.h"

/* Errors are read from stderr by default. */
#define SPAWN_READ_STDOUT	(1 << 0)

/* Internal to spawn_compile(). */
#define SPAWN_PRINT_ERRORS	(1 << 1)

/* Redirect to /dev/null? */
#define SPAWN_REDIRECT_STDOUT	(1 << 2)
#define SPAWN_REDIRECT_STDERR	(1 << 3)

/* Press any key to continue */
#define SPAWN_PROMPT		(1 << 4)

struct filter_data {
	char *in;
	char *out;
	unsigned int in_len;
	unsigned int out_len;
};

int spawn_filter(char **argv, struct filter_data *data);
void spawn_compiler(char **args, unsigned int flags, struct compiler *c);
void spawn(char **args, int fd[3], int prompt);

#endif
