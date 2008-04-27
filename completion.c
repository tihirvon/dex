#include "commands.h"
#include "cmdline.h"

#include <sys/types.h>
#include <dirent.h>

struct {
	char *head;
	char *tail;
	char **matches;
	int count;
	int alloc;
	int idx;

	// should we add space after completed string if we have only one match?
	int add_space;
} completion;

static void add_completion(char *str)
{
	if (completion.count == completion.alloc) {
		completion.alloc = (completion.count + 8) & ~7;
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

static int strptrcmp(const void *ap, const void *bp)
{
	const char *a = *(const char **)ap;
	const char *b = *(const char **)bp;
	return strcmp(a, b);
}

static void collect_files(const char *prefix)
{
	GBUF(buf);
	const char *slash;
	int pos = 0;

	if (*prefix == '~') {
		int len;

		slash = strchr(prefix + 1, '/');
		if (slash) {
			len = slash - prefix - 1;
		} else {
			len = strlen(prefix + 1);
		}

		if (!len) {
			// replace ~ with $HOME
			gbuf_add_str(&buf, home_dir);
			pos = 1;
		} else {
			const char *str = get_home_dir(prefix + 1, len);
			if (str) {
				gbuf_add_str(&buf, str);
				pos = 1 + len;
			}
		}
	}
	slash = strrchr(prefix + pos, '/');
	if (slash) {
		// prefix starts with
		//
		// ~/
		// ~user/
		// ~invaliduser/     (NOTE: can still be valid directory!)
		// /
		// foo/
		char *dirprefix = xstrndup(prefix, slash - prefix + 1);
		gbuf_add_buf(&buf, prefix + pos, slash - prefix - pos + 1);
		do_collect_files(buf.buffer, dirprefix, slash + 1);
		free(dirprefix);
	} else {
		// prefix is (no slashes at all)
		//
		// ~
		// ~user
		// ~invaliduser
		// ~partialuser
		// foo
		if (pos) {
			// buf is valid home directory (~ or ~user expanded)
			// complete prefix + pos relative to the directory
			char *dirprefix = xnew(char, pos + 2);
			memcpy(dirprefix, prefix, pos);
			dirprefix[pos] = '/';
			dirprefix[pos + 1] = 0;
			gbuf_add_ch(&buf, '/');
			do_collect_files(buf.buffer, dirprefix, prefix + pos);
			free(dirprefix);
		} else {
			if (*prefix == '~') {
				// FIXME: complete usernames
				// fallback to completing filenames
			}
			// complete prefix relative to "."
			do_collect_files("./", "", prefix);
		}
	}
	gbuf_free(&buf);
	if (completion.count > 1)
		qsort(completion.matches, completion.count, sizeof(*completion.matches), strptrcmp);
	if (completion.count == 1) {
		// if we have only one match we add space after completed
		// string for files, not directories
		int len = strlen(completion.matches[0]);
		completion.add_space = completion.matches[0][len - 1] != '/';
	}
}

static void collect_completions(struct parsed_command *pc, const char *str)
{
	const struct command *cmd;

	if (!pc->args_before_cursor) {
		collect_commands(str);
		return;
	}
	cmd = find_command(pc->argv[0]);
	if (cmd) {
		if (!strcmp(cmd->name, "open") || !strcmp(cmd->name, "save")) {
			collect_files(str);
			return;
		}
	}
}

static void init_completion(void)
{
	struct parsed_command pc;
	const char *cmd = cmdline.buffer;
	char *str;
	int rc;

	rc = parse_commands(&pc, cmd, cmdline_pos);
	str = xstrndup(cmd + pc.comp_so, pc.comp_eo - pc.comp_so);
	d_print("%c %d %d %d |%s|\n", rc, pc.args_before_cursor, pc.comp_so, pc.comp_eo, str);

	completion.head = xstrndup(cmd, pc.comp_so);
	completion.tail = xstrdup(cmd + pc.comp_eo);
	completion.add_space = 1;

	collect_completions(&pc, str);
	free(str);
	free_commands(&pc);
}

void complete_command(void)
{
	const char *middle;
	char *str;
	int head_len, middle_len, tail_len;

	if (!completion.head)
		init_completion();
	if (!completion.count)
		return;

	middle = completion.matches[completion.idx];
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

	free(str);
	completion.idx = (completion.idx + 1) % completion.count;
	if (completion.count == 1)
		reset_completion();
}

void reset_completion(void)
{
	int i;

	free(completion.head);
	free(completion.tail);
	for (i = 0; i < completion.count; i++)
		free(completion.matches[i]);
	free(completion.matches);
	memset(&completion, 0, sizeof(completion));
}
