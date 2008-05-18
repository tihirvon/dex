#include "spawn.h"
#include "editor.h"
#include "buffer.h"
#include "window.h"
#include "util.h"

struct error_pattern {
	/*
	 * 0  Useless.  Always ignored.
	 * 1  Useful only if there are no other errors, redundant otherwise.
	 *    E.g. a command in Makefile fails silently and make complains.
	 * 2  Redundant.  The real error message follows.
	 * 3  Important.
	 */
	int importance;
	int msg_idx;
	int file_idx;
	int line_idx;
	const char *pattern;
};

static const struct error_pattern error_patterns[] = {
	{  0,  0, -1, -1, "error: \\(Each undeclared identifier is reported only once" },
	{  0,  0, -1, -1, "error: for each function it appears in.\\)" },
	{  3,  3,  1,  2, "^(.+):([0-9]+): (.*)" },
	{  3,  0,  1,  2, "^.* at (.+):([0-9]+):$" },
	{  2,  0,  1,  2, "^In file included from (.+):([0-9]+)[,:]" },
	{  2,  0,  1,  2, "^ +from (.+):([0-9]+)[,:]" },
	{  2,  2,  1, -1, "^(.+): (In function .*:)$" },
	{  1,  0, -1, -1, "^make: \\*\\*\\* \\[.*\\] Error [0-9]+$" },
	{  1,  0, -1, -1, "^collect2: ld returned [0-9]+ exit status$" },
	{ -1, -1, -1, -1, NULL }
};

struct compile_errors cerr;

static void free_compile_error(struct compile_error *e)
{
	free(e->msg);
	free(e->file);
	free(e);
}

static void free_errors(void)
{
	int i;

	for (i = 0; i < cerr.count; i++)
		free_compile_error(cerr.errors[i]);
	free(cerr.errors);
	cerr.errors = NULL;
	cerr.alloc = 0;
	cerr.count = 0;
	cerr.pos = -1;
}

static int is_duplicate(const struct compile_error *e)
{
	int i;

	for (i = 0; i < cerr.count; i++) {
		struct compile_error *x = cerr.errors[i];
		if (e->line != x->line)
			continue;
		if (!e->file != !x->file)
			continue;
		if (e->file && strcmp(e->file, x->file))
			continue;
		if (strcmp(e->msg, x->msg))
			continue;
		return 1;
	}
	return 0;
}

static void add_error_msg(struct compile_error *e, unsigned int flags)
{
	if (flags & SPAWN_IGNORE_DUPLICATES && is_duplicate(e)) {
		free_compile_error(e);
		return;
	}
	if (cerr.count == cerr.alloc) {
		cerr.alloc = ROUND_UP(cerr.alloc * 3 / 2 + 1, 16);
		xrenew(cerr.errors, cerr.alloc);
	}
	cerr.errors[cerr.count++] = e;
}

static void handle_error_msg(char *str, unsigned int flags)
{
	const struct error_pattern *p;
	struct compile_error *e;
	char *nl = strchr(str, '\n');
	int min_level, i;

	if (nl)
		*nl = 0;
	fprintf(stderr, "%s\n", str);

	for (i = 0; ; i++) {
		p = &error_patterns[i];
		if (!p->pattern) {
			e = xnew(struct compile_error, 1);
			e->msg = xstrdup(str);
			e->file = NULL;
			e->line = -1;
			add_error_msg(e, flags);
			return;
		}
		if (regexp_match(p->pattern, str))
			break;
	}

	min_level = 0;
	if (flags & SPAWN_IGNORE_REDUNDANT)
		min_level = 3;

	if (p->importance >= min_level || (p->importance == 1 && !cerr.count)) {
		e = xnew(struct compile_error, 1);
		e->msg = xstrdup(regexp_matches[p->msg_idx]);
		e->file = p->file_idx < 0 ? NULL : xstrdup(regexp_matches[p->file_idx]);
		e->line = p->line_idx < 0 ? -1 : atoi(regexp_matches[p->line_idx]);
		add_error_msg(e, flags);
	}
	free_regexp_matches();
}

static void read_stderr(int fd, unsigned int flags)
{
	FILE *f = fdopen(fd, "r");
	char line[4096];

	free_errors();

	while (fgets(line, sizeof(line), f))
		handle_error_msg(line, flags);
	fclose(f);
}

void spawn(char **args, unsigned int flags)
{
	unsigned int mask = SPAWN_REDIRECT_STDOUT | SPAWN_REDIRECT_STDERR;
	int quiet = (flags & mask) == mask;
	pid_t pid;
	int status;
	int p[2];

	if (flags & SPAWN_COLLECT_ERRORS && pipe(p)) {
		error_msg("pipe: %s", strerror(errno));
		return;
	}

	if (!quiet)
		ui_end();
	pid = fork();
	if (pid < 0) {
		int error = errno;
		if (!quiet)
			ui_start(0);
		error_msg("fork: %s", strerror(error));
		return;
	}
	if (!pid) {
		int i, dev_null = -1;

		if (flags & (SPAWN_REDIRECT_STDOUT | SPAWN_REDIRECT_STDERR))
			dev_null = open("/dev/null", O_WRONLY);
		if (dev_null != -1) {
			if (flags & SPAWN_REDIRECT_STDOUT)
				dup2(dev_null, 1);
			if (flags & SPAWN_REDIRECT_STDERR)
				dup2(dev_null, 2);
		}
		if (flags & SPAWN_COLLECT_ERRORS)
			dup2(p[1], 2);
		for (i = 3; i < 30; i++)
			close(i);
		execvp(args[0], args);
		exit(42);
	}
	if (flags & SPAWN_COLLECT_ERRORS) {
		close(p[1]);
		read_stderr(p[0], flags);
		close(p[0]);
	}
	while (wait(&status) < 0 && errno == EINTR)
		;
	if (!quiet)
		ui_start(flags & SPAWN_PROMPT);
	if (flags & SPAWN_JUMP_TO_ERROR && cerr.count) {
		cerr.pos = 0;
		show_compile_error();
	}
}

static void goto_file_line(const char *filename, int line)
{
	struct view *v = open_buffer(filename);

	if (!v) {
		return;
	}
	set_view(v);
	move_to_line(line);
}

void show_compile_error(void)
{
	struct compile_error *e = cerr.errors[cerr.pos];

	if (e->file && e->line > 0) {
		goto_file_line(e->file, e->line);
	} else if (e->file && cerr.pos + 1 < cerr.count) {
		struct compile_error *next = cerr.errors[cerr.pos + 1];
		if (next->file && next->line > 0)
			goto_file_line(next->file, next->line);
	}
	info_msg("[%d/%d] %s", cerr.pos + 1, cerr.count, e->msg);
}
