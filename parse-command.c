#include "commands.h"
#include "util.h"
#include "gbuf.h"

// can contain many commands. each terminated with NULL
static char **argv;
static int argc;
static int arga;
static GBUF(arg);

static void add_arg(char *str)
{
	if (arga == argc) {
		arga = (argc + 1 + 7) & ~7;
		xrenew(argv, arga);
	}
	argv[argc++] = str;
}

static const char *get_home_dir(const char *username, int len)
{
	char buf[len + 1];
	struct passwd *passwd;

	memcpy(buf, username, len);
	buf[len] = 0;
	passwd = getpwnam(buf);
	if (!passwd)
		return NULL;
	return passwd->pw_dir;
}

static int parse_home(const char *cmd, int *posp)
{
	int len, pos = *posp;
	const char *username = cmd + pos + 1;
	const char *str;

	for (len = 0; username[len]; len++) {
		char ch = username[len];
		if (isspace(ch) || ch == '/' || ch == ':')
			break;
		if (!isalnum(ch))
			return 0;
	}

	if (!len) {
		gbuf_add_str(&arg, home_dir);
		*posp = pos + 1;
		return 1;
	}

	str = get_home_dir(username, len);
	if (!str)
		return 0;
	gbuf_add_str(&arg, str);
	*posp = pos + 1 + len;
	return 1;
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

		if (!got_arg && cmd[pos] == '~') {
			got_arg = 1;
			if (parse_home(cmd, &pos))
				continue;
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

char **parse_commands(const char *cmd, int *argcp)
{
	int pos = 0;

	argc = 0;
	while (1) {
		if (parse_command(cmd, &pos))
			return NULL;
		if (!cmd[pos] || cmd[pos] == '#')
			break;
		pos++;
	}

	*argcp = argc;
	return argv;
}
