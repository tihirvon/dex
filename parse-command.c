#include "commands.h"
#include "util.h"
#include "gbuf.h"

static GBUF(arg);

static void add_arg(struct parsed_command *pc, char *str)
{
	if (pc->alloc == pc->count) {
		pc->alloc = (pc->count + 1 + 7) & ~7;
		xrenew(pc->argv, pc->alloc);
	}
	pc->argv[pc->count++] = str;
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

static int parse_command(struct parsed_command *pc, const char *cmd, int *posp)
{
	int got_arg = 0;
	int pos = *posp;

	while (1) {
		if (isspace(cmd[pos])) {
			if (pos < pc->comp_eo)
				pc->comp_so = -1;

			if (got_arg)
				add_arg(pc, gbuf_steal(&arg));
			got_arg = 0;
			while (isspace(cmd[pos]))
				pos++;
		}

		if (pos <= pc->comp_eo) {
			if (!got_arg) {
				pc->comp_so = pos;
				pc->args_before_cursor = pc->count;
			}
		} else if (pc->comp_so < 0) {
			pc->comp_so = pc->comp_eo;
			pc->args_before_cursor = pc->count;
		}

		if (!cmd[pos] || cmd[pos] == '#' || cmd[pos] == ';') {
			if (got_arg)
				add_arg(pc, gbuf_steal(&arg));
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
	add_arg(pc, NULL);
	*posp = pos;
	return 0;
error:
	gbuf_free(&arg);
	d_print("parse error\n");
	return -1;
}

int parse_commands(struct parsed_command *pc, const char *cmd, int cursor_pos)
{
	int pos = 0;

	memset(pc, 0, sizeof(*pc));
	pc->comp_so = -1;
	pc->comp_eo = cursor_pos;

	while (1) {
		if (parse_command(pc, cmd, &pos))
			return -1;
		if (!cmd[pos] || cmd[pos] == '#')
			break;
		pos++;
	}
	return 0;
}

void free_commands(struct parsed_command *pc)
{
	while (pc->count > 0)
		free(pc->argv[--pc->count]);
	free(pc->argv);
}
