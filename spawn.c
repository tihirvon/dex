#include "spawn.h"
#include "buffer.h"
#include "window.h"

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

static struct compile_error *parse_error_msg(char *str)
{
	struct compile_error *e = xnew(struct compile_error, 1);
	char *nl = strchr(str, '\n');
	char *colon, *ptr;

	if (nl)
		*nl = 0;
	fprintf(stderr, "%s\n", str);

	e->file = NULL;
	e->line = -1;

	colon = strchr(str, ':');
	ptr = colon + 1;
	if (colon && isdigit(*ptr)) {
		int num = 0;

		*colon = 0;
		while (isdigit(*ptr)) {
			num *= 10;
			num += *ptr++ - '0';
		}
		e->file = xstrdup(str);
		e->line = num;

		if (*ptr == ':')
			ptr++;
		while (isspace(*ptr))
			ptr++;
		str = ptr;
	}
	e->msg = xstrdup(str);
	return e;
}

static int is_redundant(const char *msg)
{
	if (strstr(msg, ": In function "))
		return 1;

	// don't ignore these if there are no other errors
	if (regexp_match("^make: \\*\\*\\* \\[.*\\] Error 1$", msg) ||
	    regexp_match("^collect.*: ld returned 1 exit status$", msg))
		return cerr.count;
	return 0;
}

static void read_stderr(int fd, unsigned int flags)
{
	FILE *f = fdopen(fd, "r");
	char line[4096];

	free_errors();

	while (fgets(line, sizeof(line), f)) {
		struct compile_error *e = parse_error_msg(line);

		if (!strcmp(e->msg, "error: (Each undeclared identifier is reported only once") ||
		    !strcmp(e->msg, "error: for each function it appears in.)")) {
			free_compile_error(e);
			continue;
		}
		if (flags & SPAWN_IGNORE_REDUNDANT && is_redundant(e->msg)) {
			free_compile_error(e);
			continue;
		}
		if (cerr.count == cerr.alloc) {
			cerr.alloc = (cerr.alloc * 3 / 2 + 16) & ~15;
			xrenew(cerr.errors, cerr.alloc);
		}
		cerr.errors[cerr.count++] = e;
	}
	fclose(f);
}

void spawn(char **args, unsigned int flags)
{
	pid_t pid;
	int status;
	int p[2];

	if (flags & SPAWN_COLLECT_ERRORS && pipe(p)) {
		error_msg("pipe: %s", strerror(errno));
		return;
	}

	ui_end();
	pid = fork();
	if (pid < 0) {
		int error = errno;
		ui_start();
		update_everything();
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
	ui_start();
	if (flags & SPAWN_PROMPT)
		any_key();
	update_everything();
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
	} else if (strstr(e->msg, ": In function ") && cerr.pos + 1 < cerr.count) {
		struct compile_error *next = cerr.errors[cerr.pos + 1];
		if (next->file && next->line > 0)
			goto_file_line(next->file, next->line);
	}
	info_msg("[%d/%d] %s", cerr.pos + 1, cerr.count, e->msg);
}
