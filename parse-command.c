#include "commands.h"
#include "util.h"
#include "gbuf.h"
#include "buffer.h"

static GBUF(arg);

static void add_arg(struct parsed_command *pc, char *str)
{
	if (pc->alloc == pc->count) {
		pc->alloc = ROUND_UP(pc->count + 1, 8);
		xrenew(pc->argv, pc->alloc);
	}
	pc->argv[pc->count++] = str;
}

static void parse_home(const char *cmd, int *posp)
{
	int len, pos = *posp;
	const char *username = cmd + pos + 1;
	struct passwd *passwd;
	char buf[64];

	for (len = 0; username[len]; len++) {
		char ch = username[len];
		if (isspace(ch) || ch == '/' || ch == ':')
			break;
		if (!isalnum(ch))
			return;
	}

	if (!len) {
		gbuf_add_str(&arg, home_dir);
		*posp = pos + 1;
		return;
	}

	if (len >= sizeof(buf))
		return;

	memcpy(buf, username, len);
	buf[len] = 0;
	passwd = getpwnam(buf);
	if (!passwd)
		return;

	gbuf_add_str(&arg, passwd->pw_dir);
	*posp = pos + 1 + len;
}

static void parse_sq(const char *cmd, int *posp)
{
	int pos = *posp;

	while (1) {
		if (cmd[pos] == '\'') {
			pos++;
			break;
		}
		if (!cmd[pos])
			break;
		gbuf_add_ch(&arg, cmd[pos++]);
	}
	*posp = pos;
}

static void parse_dq(const char *cmd, int *posp)
{
	int pos = *posp;

	while (1) {
		if (cmd[pos] == '"') {
			pos++;
			break;
		}
		if (!cmd[pos])
			break;
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
}

char *parse_command_arg(const char *cmd, int tilde)
{
	int pos = 0;

	if (tilde && cmd[pos] == '~')
		parse_home(cmd, &pos);
	while (1) {
		char ch = cmd[pos];

		if (!ch || ch == ';' || isspace(ch))
			break;

		pos++;
		if (ch == '\'') {
			parse_sq(cmd, &pos);
		} else if (ch == '"') {
			parse_dq(cmd, &pos);
		} else if (ch == '\\') {
			if (!cmd[pos])
				break;
			gbuf_add_ch(&arg, cmd[pos++]);
		} else {
			gbuf_add_ch(&arg, ch);
		}
	}
	return gbuf_steal(&arg);
}

static int find_end(const char *cmd, int *posp)
{
	int pos = *posp;

	while (1) {
		char ch = cmd[pos];

		if (!ch || ch == ';' || isspace(ch))
			break;

		pos++;
		if (ch == '\'') {
			while (1) {
				if (cmd[pos] == '\'') {
					pos++;
					break;
				}
				if (!cmd[pos]) {
					error_msg("Missing '");
					return -1;
				}
				pos++;
			}
		} else if (ch == '"') {
			while (1) {
				if (cmd[pos] == '"') {
					pos++;
					break;
				}
				if (!cmd[pos]) {
					error_msg("Missing \"");
					return -1;
				}
				if (cmd[pos++] == '\\') {
					if (!cmd[pos])
						goto unexpected_eof;
					pos++;
				}
			}
		} else if (ch == '\\') {
			if (!cmd[pos])
				goto unexpected_eof;
			pos++;
		}
	}
	*posp = pos;
	return 0;
unexpected_eof:
	error_msg("Unexpected EOF");
	return -1;
}

static int parse_command(struct parsed_command *pc, const char *cmd, int *posp)
{
	int sidx, pos = *posp;

	while (1) {
		while (isspace(cmd[pos]))
			pos++;

		if (pc->comp_so < 0 && pos >= pc->comp_eo) {
			pc->comp_so = pc->comp_eo;
			pc->args_before_cursor = pc->count;
		}

		if (!cmd[pos] || cmd[pos] == ';')
			break;

		sidx = pos;
		if (find_end(cmd, &pos)) {
			if (pc->comp_eo > sidx) {
				pc->comp_so = sidx;
				pc->args_before_cursor = pc->count;
			}
			return -1;
		}

		if (pc->comp_eo > sidx && pc->comp_eo <= pos) {
			pc->comp_so = sidx;
			pc->args_before_cursor = pc->count;
		}

		add_arg(pc, parse_command_arg(cmd + sidx, 1));
	}
	add_arg(pc, NULL);
	*posp = pos;
	return 0;
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
		if (!cmd[pos])
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
