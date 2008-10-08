#include "commands.h"
#include "cmdline.h"
#include "options.h"
#include "util.h"
#include "gbuf.h"

struct {
	// part of string which is to be replaced
	char *escaped;
	char *parsed;

	char *head;
	char *tail;
	char **matches;
	int count;
	int alloc;
	int idx;

	// should we add space after completed string if we have only one match?
	int add_space;

	int tilde_expanded;
} completion;

void add_completion(char *str)
{
	if (completion.count == completion.alloc) {
		completion.alloc = ROUND_UP(completion.count + 1, 8);
		xrenew(completion.matches, completion.alloc);
	}
	completion.matches[completion.count++] = str;
}

static void collect_commands(const char *prefix)
{
	int prefix_len = strlen(prefix);
	int i;

	for (i = 0; commands[i].name; i++) {
		const struct command *c = &commands[i];

		if (!strncmp(prefix, c->name, prefix_len)) {
			add_completion(xstrdup(c->name));
			continue;
		}
		if (c->short_name && !strncmp(prefix, c->short_name, prefix_len))
			add_completion(xstrdup(c->name));
	}
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

static int strptrcmp(const void *ap, const void *bp)
{
	const char *a = *(const char **)ap;
	const char *b = *(const char **)bp;
	return strcmp(a, b);
}

static void collect_and_sort_files(void)
{
	collect_files();
	if (completion.count > 1)
		qsort(completion.matches, completion.count, sizeof(*completion.matches), strptrcmp);
	if (completion.count == 1) {
		// if we have only one match we add space after completed
		// string for files, not directories
		int len = strlen(completion.matches[0]);
		completion.add_space = completion.matches[0][len - 1] != '/';
	}
}

static void collect_completions(struct parsed_command *pc)
{
	const struct command *cmd;
	int name_idx;
	int i;

	// Multiple commands are separated by ";" which are converted to NULL.
	// Find command name.
	name_idx = -1;
	for (i = 0; i < pc->args_before_cursor; i++) {
		if (name_idx == -1)
			name_idx = i;
		if (!pc->argv[i])
			name_idx = -1;
	}

	if (name_idx < 0) {
		collect_commands(completion.parsed);
		return;
	}
	cmd = find_command(commands, pc->argv[name_idx]);
	if (cmd) {
		int argc = pc->args_before_cursor - name_idx;
		if (!strcmp(cmd->name, "open") || !strcmp(cmd->name, "save") || !strcmp(cmd->name, "include")) {
			collect_and_sort_files();
			return;
		}
		if (!strcmp(cmd->name, "set")) {
			if (argc == 1) {
				collect_options(completion.parsed);
			} else if (argc == 2) {
				collect_option_values(pc->argv[pc->args_before_cursor - 1], completion.parsed);
			}
			return;
		}
		if (!strcmp(cmd->name, "toggle") && argc == 1)
			collect_options(completion.parsed);
	}
}

static void init_completion(void)
{
	struct parsed_command pc;
	const char *cmd = cmdline.buffer;
	int rc;

	rc = parse_commands(&pc, cmd, cmdline_pos);
	if (pc.comp_so < 0) {
		// trying to complete comment
		free_commands(&pc);
		return;
	}
	completion.escaped = xstrndup(cmd + pc.comp_so, pc.comp_eo - pc.comp_so);
	completion.parsed = parse_command_arg(completion.escaped, 1);
	completion.head = xstrndup(cmd, pc.comp_so);
	completion.tail = xstrdup(cmd + pc.comp_eo);
	completion.add_space = 1;

	collect_completions(&pc);
	free_commands(&pc);
}

static char *shell_escape(const char *str)
{
	GBUF(buf);
	int i;

	if (str[0] == '~' && !completion.tilde_expanded)
		gbuf_add_ch(&buf, '\\');

	for (i = 0; str[i]; i++) {
		char ch = str[i];
		if (ch == ' ' || ch == '\'' || ch == '"') {
			gbuf_add_ch(&buf, '\\');
			gbuf_add_ch(&buf, ch);
		} else {
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
	if (!completion.count)
		return;

	middle = shell_escape(completion.matches[completion.idx]);
	middle_len = strlen(middle);
	head_len = strlen(completion.head);
	tail_len = strlen(completion.tail);

	str = xmalloc(head_len + middle_len + tail_len + 2);
	memcpy(str, completion.head, head_len);
	memcpy(str + head_len, middle, middle_len);
	if (completion.count == 1 && completion.add_space) {
		str[head_len + middle_len] = ' ';
		middle_len++;
	}
	memcpy(str + head_len + middle_len, completion.tail, tail_len + 1);

	cmdline_set_text(str);
	cmdline_pos = head_len + middle_len;

	free(middle);
	free(str);
	completion.idx = (completion.idx + 1) % completion.count;
	if (completion.count == 1)
		reset_completion();
}

void reset_completion(void)
{
	int i;

	free(completion.escaped);
	free(completion.parsed);
	free(completion.head);
	free(completion.tail);
	for (i = 0; i < completion.count; i++)
		free(completion.matches[i]);
	free(completion.matches);
	memset(&completion, 0, sizeof(completion));
}
