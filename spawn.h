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

struct error_format {
	enum msg_importance importance;
	signed char msg_idx;
	signed char file_idx;
	signed char line_idx;
	signed char column_idx;
	const char *pattern;
};

struct compiler_format {
	char *compiler;
	struct error_format *formats;
	int nr_formats;
};

struct filter_data {
	char *in;
	char *out;
	unsigned int in_len;
	unsigned int out_len;
};

extern struct compile_errors cerr;

void add_error_fmt(const char *compiler, enum msg_importance importance, const char *format, char **desc);
struct compiler_format *find_compiler_format(const char *name);
void spawn(char **args, unsigned int flags, struct compiler_format *cf);
int spawn_filter(char **argv, struct filter_data *data);
void show_compile_error(void);

#endif
