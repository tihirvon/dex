#include "buffer.h"

#include <ctype.h>

struct growing_buffer {
	char *buffer;
	size_t alloc;
	size_t count;
};

#define GROWING_BUFFER(name) struct growing_buffer name = { NULL, 0, 0 }

// can contain many commands. each terminated with NULL
static char **argv;
static int argc;
static int arga;
static GROWING_BUFFER(arg);

static size_t gbuf_avail(struct growing_buffer *buf)
{
	return buf->alloc - buf->count;
}

static void gbuf_resize(struct growing_buffer *buf, size_t size)
{
	size_t align = 16 - 1;

	buf->alloc = (size + align) & ~align;
	buf->buffer = xrealloc(buf->buffer, buf->alloc);
}

static void gbuf_free(struct growing_buffer *buf)
{
	free(buf->buffer);
	buf->buffer = NULL;
	buf->alloc = 0;
	buf->count = 0;
}

static void gbuf_add_ch(struct growing_buffer *buf, char ch)
{
	size_t avail = gbuf_avail(buf);

	if (avail < 1)
		gbuf_resize(buf, buf->count + 1);
	buf->buffer[buf->count++] = ch;
}

static char *gbuf_steal(struct growing_buffer *buf)
{
	char *b;

	gbuf_add_ch(buf, 0);
	b = buf->buffer;
	buf->buffer = NULL;
	buf->alloc = 0;
	buf->count = 0;
	return b;
}

static void add_arg(char *str)
{
	if (arga == argc) {
		arga = (argc + 1 + 7) & ~7;
		xrenew(argv, arga);
	}
	argv[argc++] = str;
}

static int parse_sq(const char *cmd, int *posp)
{
	int pos = *posp + 1;

	while (1) {
		if (cmd[pos] == '\'') {
			pos++;
			break;
		}
		if (!cmd[pos]) {
			return -1;
		}
		gbuf_add_ch(&arg, cmd[pos++]);
	}
	*posp = pos;
	return 0;
}

static int parse_dq(const char *cmd, int *posp)
{
	int pos = *posp + 1;

	while (1) {
		if (cmd[pos] == '"') {
			pos++;
			break;
		}
		if (!cmd[pos]) {
			return -1;
		}
		if (cmd[pos] == '\\') {
			pos++;
			switch (cmd[pos]) {
			case '\\':
			case '"':
				gbuf_add_ch(&arg, cmd[pos++]);
				break;
			default:
				gbuf_add_ch(&arg, '\\');
				gbuf_add_ch(&arg, cmd[pos++]);
				break;
			}
		} else {
			gbuf_add_ch(&arg, cmd[pos++]);
		}
	}
	*posp = pos;
	return 0;
}

static int parse_command(const char *cmd, int *posp)
{
	int got_arg = 0;
	int pos = *posp;

	while (1) {
		if (isspace(cmd[pos])) {
			if (got_arg)
				add_arg(gbuf_steal(&arg));
			got_arg = 0;
			while (isspace(cmd[pos]))
				pos++;
		}
		if (!cmd[pos] || cmd[pos] == '#' || cmd[pos] == ';') {
			if (got_arg)
				add_arg(gbuf_steal(&arg));
			got_arg = 0;
			break;
		}

		got_arg = 1;
		if (cmd[pos] == '\'') {
			if (parse_sq(cmd, &pos))
				goto error;
		} else if (cmd[pos] == '"') {
			if (parse_dq(cmd, &pos))
				goto error;
		} else if (cmd[pos] == '\\') {
			pos++;
			if (!cmd[pos])
				goto error;
			gbuf_add_ch(&arg, cmd[pos++]);
		} else {
			gbuf_add_ch(&arg, cmd[pos++]);
		}
	}
	add_arg(NULL);
	*posp = pos;
	return 0;
error:
	gbuf_free(&arg);
	d_print("parse error\n");
	return -1;
}

static int parse_commands(const char *cmd)
{
	int pos = 0;

	argc = 0;
	while (1) {
		if (parse_command(cmd, &pos))
			return -1;
		if (!cmd[pos] || cmd[pos] == '#')
			break;
		pos++;
	}
	return 0;
}

static void run_command(char **av)
{
	int i;

	for (i = 0; commands[i].name; i++) {
		if (!strcmp(av[0], commands[i].name)) {
			commands[i].cmd(av + 1);
			return;
		}
	}
	d_print("no such command: %s\n", av[0]);
}

void handle_command(const char *cmd)
{
	if (!parse_commands(cmd)) {
		int s, e;

		s = 0;
		while (s < argc) {
			e = s;
			while (e < argc && argv[e])
				e++;

			if (e > s)
				run_command(argv + s);

			s = e + 1;
		}
	}
	while (argc > 0)
		free(argv[--argc]);
}
