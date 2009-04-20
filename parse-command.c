#include "commands.h"
#include "util.h"
#include "gbuf.h"
#include "buffer.h"
#include "ptr-array.h"

static GBUF(arg);

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

int find_end(const char *cmd, int *posp)
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

int parse_commands(struct ptr_array *array, const char *cmd)
{
	int pos = 0;

	while (1) {
		int end;

		while (isspace(cmd[pos]))
			pos++;

		if (!cmd[pos])
			break;

		if (cmd[pos] == ';') {
			ptr_array_add(array, NULL);
			pos++;
			continue;
		}

		end = pos;
		if (find_end(cmd, &end))
			return -1;

		ptr_array_add(array, parse_command_arg(cmd + pos, 1));
		pos = end;
	}
	ptr_array_add(array, NULL);
	return 0;
}
