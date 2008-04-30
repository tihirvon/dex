#include "spawn.h"
#include "buffer.h"
#include "window.h"

struct compile_errors cerr;

static void free_errors(void)
{
	int i;

	for (i = 0; i < cerr.count; i++)
		free(cerr.lines[i]);
	free(cerr.lines);
	cerr.lines = NULL;
	cerr.alloc = 0;
	cerr.count = 0;
	cerr.pos = -1;
}

static void read_stderr(int fd)
{
	FILE *f = fdopen(fd, "r");
	char line[4096];

	free_errors();

	while (fgets(line, sizeof(line), f)) {
		char *nl = strchr(line, '\n');

		if (nl)
			*nl = 0;
		fprintf(stderr, "%s\n", line);
		if (cerr.count == cerr.alloc) {
			cerr.alloc = (cerr.alloc * 3 / 2 + 16) & ~15;
			xrenew(cerr.lines, cerr.alloc);
		}
		cerr.lines[cerr.count++] = xstrdup(line);
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
		read_stderr(p[0]);
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
	char *line = cerr.lines[cerr.pos];
	char *colon, *ptr;

	colon = strchr(line, ':');
	ptr = colon + 1;
	if (colon && isdigit(*ptr)) {
		int num = 0;

		*colon = 0;
		while (isdigit(*ptr)) {
			num *= 10;
			num += *ptr++ - '0';
		}
		if (*ptr == ':')
			ptr++;
		while (isspace(*ptr))
			ptr++;
		goto_file_line(line, num);
		*colon = ':';
		line = ptr;
	}
	info_msg("[%d/%d] %s", cerr.pos + 1, cerr.count, line);
}
