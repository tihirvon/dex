#include "completion.h"
#include "command.h"
#include "cmdline.h"
#include "options.h"
#include "alias.h"
#include "gbuf.h"
#include "ptr-array.h"
#include "tag.h"
#include "common.h"
#include "color.h"

static struct {
	// part of string which is to be replaced
	char *escaped;
	char *parsed;

	char *head;
	char *tail;
	struct ptr_array completions;
	int idx;

	// should we add space after completed string if we have only one match?
	int add_space;

	int tilde_expanded;
} completion;

static int directories_only;

static int strptrcmp(const void *ap, const void *bp)
{
	const char *a = *(const char **)ap;
	const char *b = *(const char **)bp;
	return strcmp(a, b);
}

static void sort_completions(void)
{
	struct ptr_array *a = &completion.completions;
	if (a->count > 1)
		qsort(a->ptrs, a->count, sizeof(*a->ptrs), strptrcmp);
}

void add_completion(char *str)
{
	ptr_array_add(&completion.completions, str);
}

static void collect_commands(const char *prefix)
{
	int prefix_len = strlen(prefix);
	int i;

	for (i = 0; commands[i].name; i++) {
		const struct command *c = &commands[i];

		if (!strncmp(prefix, c->name, prefix_len))
			add_completion(xstrdup(c->name));
	}

	collect_aliases(prefix);
}

static void do_collect_files(const char *dirname, const char *dirprefix, const char *fileprefix)
{
	char path[PATH_MAX];
	int plen = strlen(dirname);
	int dlen = strlen(dirprefix);
	int flen = strlen(fileprefix);
	struct dirent *de;
	DIR *dir;

	if (plen >= sizeof(path) - 2)
		return;

	dir = opendir(dirname);
	if (!dir)
		return;

	memcpy(path, dirname, plen);
	if (path[plen - 1] != '/')
		path[plen++] = '/';

	while ((de = readdir(dir))) {
		const char *name = de->d_name;
		struct stat st;
		int len, is_dir;
		char *c;

		if (flen) {
			if (strncmp(name, fileprefix, flen))
				continue;
		} else {
			if (name[0] == '.')
				continue;
		}

		len = strlen(name);
		if (plen + len + 2 > sizeof(path))
			continue;
		memcpy(path + plen, name, len + 1);

		if (lstat(path, &st))
			continue;

		is_dir = S_ISDIR(st.st_mode);
		if (S_ISLNK(st.st_mode)) {
			if (!stat(path, &st))
				is_dir = S_ISDIR(st.st_mode);
		}
		if (!is_dir && directories_only)
			continue;

		c = xnew(char, dlen + len + 2);
		memcpy(c, dirprefix, dlen);
		memcpy(c + dlen, name, len + 1);
		if (is_dir) {
			c[dlen + len] = '/';
			c[dlen + len + 1] = 0;
		}
		add_completion(c);
	}
	closedir(dir);
}

static void collect_files(void)
{
	const char *slash;
	char *dir, *dirprefix, *fileprefix;
	char *str = parse_command_arg(completion.escaped, 0);

	if (strcmp(completion.parsed, str)) {
		// ~ was expanded
		completion.tilde_expanded = 1;
		slash = strrchr(str, '/');
		if (!slash) {
			// complete ~ to ~/ or ~user to ~user/
			int len = strlen(str);
			char *s = xmalloc(len + 2);
			memcpy(s, str, len);
			s[len] = '/';
			s[len + 1] = 0;
			add_completion(s);
			free(str);
			return;
		}
		dirprefix = xstrndup(str, slash - str + 1);
		fileprefix = xstrdup(slash + 1);
		slash = strrchr(completion.parsed, '/');
		dir = xstrndup(completion.parsed, slash - completion.parsed + 1);
		do_collect_files(dir, dirprefix, fileprefix);
		free(dirprefix);
		free(fileprefix);
		free(dir);
		free(str);
		return;
	}
	slash = strrchr(completion.parsed, '/');
	if (!slash) {
		do_collect_files("./", "", completion.parsed);
		free(str);
		return;
	}
	dir = xstrndup(completion.parsed, slash - completion.parsed + 1);
	fileprefix = xstrdup(slash + 1);
	do_collect_files(dir, dir, fileprefix);
	free(fileprefix);
	free(dir);
	free(str);
}

static void collect_and_sort_files(void)
{
	collect_files();
	if (completion.completions.count == 1) {
		// if we have only one match we add space after completed
		// string for files, not directories
		const char *str = completion.completions.ptrs[0];
		int len = strlen(str);
		completion.add_space = str[len - 1] != '/';
	}
}

static void collect_env(const char *name, int len)
{
	static const char * const builtin[] = {
		"FILE",
		"PKGDATADIR",
		"WORD",
	};
	extern char **environ;
	int i;

	for (i = 0; environ[i]; i++) {
		const char *e = environ[i];

		if (strncmp(e, name, len) == 0) {
			const char *end = strchr(e, '=');
			if (end)
				add_completion(xstrndup(e, end - e));
		}
	}
	for (i = 0; i < ARRAY_COUNT(builtin); i++) {
		if (strncmp(builtin[i], name, len) == 0)
			add_completion(xstrdup(builtin[i]));
	}
}

static void collect_completions(char **args, int argc)
{
	const struct command *cmd;

	if (!argc) {
		collect_commands(completion.parsed);
		return;
	}

	cmd = find_command(commands, args[0]);
	if (!cmd)
		return;

	if (!strcmp(cmd->name, "open") ||
	    !strcmp(cmd->name, "wsplit") ||
	    !strcmp(cmd->name, "save") ||
	    !strcmp(cmd->name, "compile") ||
	    !strcmp(cmd->name, "run") ||
	    !strcmp(cmd->name, "pass-through") ||
	    !strcmp(cmd->name, "include")) {
		directories_only = 0;
		collect_and_sort_files();
		return;
	}
	if (!strcmp(cmd->name, "cd")) {
		directories_only = 1;
		collect_and_sort_files();
		return;
	}
	if (!strcmp(cmd->name, "hi")) {
		switch (argc) {
		case 1:
			collect_hl_colors(completion.parsed);
			break;
		default:
			collect_colors_and_attributes(completion.parsed);
			break;
		}
		return;
	}
	if (!strcmp(cmd->name, "set")) {
		if (argc % 2) {
			collect_options(completion.parsed);
		} else {
			collect_option_values(args[argc - 1], completion.parsed);
		}
		return;
	}
	if (!strcmp(cmd->name, "toggle") && argc == 1) {
		collect_toggleable_options(completion.parsed);
		return;
	}
	if (!strcmp(cmd->name, "tag") && argc == 1) {
		collect_tags(completion.parsed);
		return;
	}
}

static void init_completion(void)
{
	const char *str, *cmd = cmdline.buffer;
	PTR_ARRAY(array);
	int semicolon = -1;
	int completion_pos = -1;
	int len, pos = 0;

	while (1) {
		int end;

		while (isspace(cmd[pos]))
			pos++;

		if (pos >= cmdline_pos) {
			completion_pos = cmdline_pos;
			break;
		}

		if (!cmd[pos])
			break;

		if (cmd[pos] == ';') {
			semicolon = array.count;
			ptr_array_add(&array, NULL);
			pos++;
			continue;
		}

		end = pos;
		if (find_end(cmd, &end) || end >= cmdline_pos) {
			completion_pos = pos;
			break;
		}

		if (semicolon + 1 == array.count) {
			char *name = xstrndup(cmd + pos, end - pos);
			const char *value = find_alias(name);

			if (value) {
				int i, save = array.count;

				if (parse_commands(&array, value)) {
					for (i = save; i < array.count; i++) {
						free(array.ptrs[i]);
						array.ptrs[i] = NULL;
					}
					array.count = save;
					ptr_array_add(&array, parse_command_arg(name, 1));
				} else {
					// Remove NULL
					array.count--;
				}
			} else {
				ptr_array_add(&array, parse_command_arg(name, 1));
			}
			free(name);
		} else {
			ptr_array_add(&array, parse_command_arg(cmd + pos, 1));
		}
		pos = end;
	}

	str = cmd + completion_pos;
	len = cmdline_pos - completion_pos;
	if (len && str[0] == '$') {
		int i, var = 1;
		for (i = 1; i < len; i++) {
			char ch = str[i];
			if (isalpha(ch) || ch == '_')
				continue;
			if (i > 1 && isdigit(ch))
				continue;

			var = 0;
			break;
		}
		if (var) {
			completion_pos++;
			completion.escaped = NULL;
			completion.parsed = NULL;
			completion.head = xstrndup(cmd, completion_pos);
			completion.tail = xstrdup(cmd + cmdline_pos);
			collect_env(str + 1, len - 1);
			sort_completions();
			ptr_array_free(&array);
			return;
		}
	}

	completion.escaped = xstrndup(str, len);
	completion.parsed = parse_command_arg(completion.escaped, 1);
	completion.head = xstrndup(cmd, completion_pos);
	completion.tail = xstrdup(cmd + cmdline_pos);
	completion.add_space = 1;

	collect_completions((char **)array.ptrs + semicolon + 1, array.count - semicolon - 1);
	sort_completions();
	ptr_array_free(&array);
}

static char *escape(const char *str)
{
	GBUF(buf);
	int i;

	if (!str[0])
		return xstrdup("\"\"");

	if (str[0] == '~' && !completion.tilde_expanded)
		gbuf_add_ch(&buf, '\\');

	for (i = 0; str[i]; i++) {
		char ch = str[i];
		switch (ch) {
		case ' ':
		case '"':
		case '$':
		case '\'':
		case '*':
		case ';':
		case '?':
		case '[':
		case '{':
			gbuf_add_ch(&buf, '\\');
			gbuf_add_ch(&buf, ch);
			break;
		default:
			gbuf_add_ch(&buf, ch);
		}
	}
	return gbuf_steal(&buf);
}

void complete_command(void)
{
	char *middle, *str;
	int head_len, middle_len, tail_len;

	if (!completion.head)
		init_completion();
	if (!completion.completions.count)
		return;

	middle = escape(completion.completions.ptrs[completion.idx]);
	middle_len = strlen(middle);
	head_len = strlen(completion.head);
	tail_len = strlen(completion.tail);

	str = xmalloc(head_len + middle_len + tail_len + 2);
	memcpy(str, completion.head, head_len);
	memcpy(str + head_len, middle, middle_len);
	if (completion.completions.count == 1 && completion.add_space) {
		str[head_len + middle_len] = ' ';
		middle_len++;
	}
	memcpy(str + head_len + middle_len, completion.tail, tail_len + 1);

	cmdline_set_text(str);
	cmdline_pos = head_len + middle_len;

	free(middle);
	free(str);
	completion.idx = (completion.idx + 1) % completion.completions.count;
	if (completion.completions.count == 1)
		reset_completion();
}

void reset_completion(void)
{
	free(completion.escaped);
	free(completion.parsed);
	free(completion.head);
	free(completion.tail);
	ptr_array_free(&completion.completions);
	clear(&completion);
}
