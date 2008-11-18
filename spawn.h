#ifndef SPAWN_H
#define SPAWN_H

/* Mutually exclusive, only one pipe supported. */
#define SPAWN_PIPE_STDOUT	(1 << 0)
#define SPAWN_PIPE_STDERR	(1 << 1)

/* Redirect to /dev/null? */
#define SPAWN_REDIRECT_STDOUT	(1 << 2)
#define SPAWN_REDIRECT_STDERR	(1 << 3)

/* Press any key to continue */
#define SPAWN_PROMPT		(1 << 4)

/* Error collection. Use with SPAWN_PIPE_*. */
#define SPAWN_COLLECT_ERRORS	(1 << 5)
#define SPAWN_IGNORE_REDUNDANT	(1 << 6)
#define SPAWN_IGNORE_DUPLICATES	(1 << 7)
#define SPAWN_JUMP_TO_ERROR	(1 << 8)

struct compile_error {
	char *file;
	char *msg;
	int line;
	int column;
};

struct compile_errors {
	struct compile_error **errors;
	int alloc;
	int count;
	int pos;
};

enum msg_importance {
	USELESS,
	REDUNDANT,
	IMPORTANT,
};

extern struct compile_errors cerr;

void add_error_fmt(const char *compiler, enum msg_importance importance, const char *format, char **desc);
void spawn(char **args, unsigned int flags, const char *compiler);
void show_compile_error(void);

#endif
