#ifndef SPAWN_H
#define SPAWN_H

#define SPAWN_PROMPT		(1 << 0)
#define SPAWN_COLLECT_ERRORS	(1 << 1)
#define SPAWN_JUMP_TO_ERROR	(1 << 2)
#define SPAWN_REDIRECT_STDOUT	(1 << 3)
#define SPAWN_REDIRECT_STDERR	(1 << 4)

struct compile_errors {
	char **lines;
	int alloc;
	int count;
	int pos;
};

extern struct compile_errors cerr;

void spawn(char **args, unsigned int flags);
void show_compile_error(void);

#endif
