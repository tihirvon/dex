#include "buffer.h"
#include "gbuf.h"

#include <ctype.h>

// can contain many commands. each terminated with NULL
static char **argv;
static int argc;
static int arga;
static GROWING_BUFFER(arg);

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
