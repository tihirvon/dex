#include "spawn.h"
#include "editor.h"
#include "buffer.h"
#include "window.h"
#include "move.h"
#include "regexp.h"
#include "gbuf.h"

struct error_format {
	enum msg_importance importance;
	signed char msg_idx;
	signed char file_idx;
	signed char line_idx;
	signed char column_idx;
	const char *pattern;
};

struct compiler_format {
	char *compiler;
	struct error_format *formats;
	int nr_formats;
};

static struct compiler_format **compiler_formats;
static int nr_compiler_formats;

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

static void handle_error_msg(struct compiler_format *cf, char *str, unsigned int flags)
{
	const struct error_format *p;
	struct compile_error *e;
	char *nl = strchr(str, '\n');
	int min_level, i;

	if (nl)
		*nl = 0;
	fprintf(stderr, "%s\n", str);

	for (i = 0; ; i++) {
		if (i == cf->nr_formats) {
			e = xnew(struct compile_error, 1);
			e->msg = xstrdup(str);
			e->file = NULL;
			e->line = -1;
			add_error_msg(e, flags);
			return;
		}
		p = &cf->formats[i];
		if (regexp_match(p->pattern, str))
			break;
	}

	min_level = 0;
	if (flags & SPAWN_IGNORE_REDUNDANT)
		min_level = 2;

	if (p->importance >= min_level) {
		e = xnew(struct compile_error, 1);
		e->msg = xstrdup(regexp_matches[p->msg_idx]);
		e->file = p->file_idx < 0 ? NULL : xstrdup(regexp_matches[p->file_idx]);
		e->line = p->line_idx < 0 ? -1 : atoi(regexp_matches[p->line_idx]);
		e->column = p->column_idx < 0 ? -1 : atoi(regexp_matches[p->column_idx]);
		add_error_msg(e, flags);
	}
	free_regexp_matches();
}

static void read_stderr(struct compiler_format *cf, int fd, unsigned int flags)
{
	FILE *f = fdopen(fd, "r");
	char line[4096];

	free_errors();

	while (fgets(line, sizeof(line), f))
		handle_error_msg(cf, line, flags);
	fclose(f);
}

static void filter(int rfd, int wfd, struct filter_data *fdata)
{
	unsigned int wlen = 0;
	GBUF(buf);
	int rc;

	if (!fdata->in_len) {
		close(wfd);
		wfd = -1;
	}
	while (1) {
		fd_set rfds, wfds;
		fd_set *wfdsp = NULL;
		int fd_high = rfd;

		FD_ZERO(&rfds);
		FD_SET(rfd, &rfds);

		if (wfd >= 0) {
			FD_ZERO(&wfds);
			FD_SET(wfd, &wfds);
			wfdsp = &wfds;
		}
		if (wfd > fd_high)
			fd_high = wfd;

		rc = select(fd_high + 1, &rfds, wfdsp, NULL, NULL);
		if (rc < 0) {
			if (errno == EINTR)
				continue;
			error_msg("select: %s", strerror(errno));
			break;
		}

		if (FD_ISSET(rfd, &rfds)) {
			char data[8192];

			rc = read(rfd, data, sizeof(data));
			if (rc < 0) {
				error_msg("read: %s", strerror(errno));
				break;
			}
			if (!rc) {
				if (wlen < fdata->in_len)
					error_msg("Command did not read all data.");
				break;
			}
			gbuf_add_buf(&buf, data, rc);
		}
		if (wfdsp && FD_ISSET(wfd, &wfds)) {
			rc = write(wfd, fdata->in + wlen, fdata->in_len - wlen);
			if (rc < 0) {
				error_msg("write: %s", strerror(errno));
				break;
			}
			wlen += rc;
			if (wlen == fdata->in_len) {
				close(wfd);
				wfd = -1;
			}
		}
	}

	if (buf.len) {
		fdata->out_len = buf.len;
		fdata->out = gbuf_steal(&buf);
	} else {
		fdata->out_len = 0;
		fdata->out = NULL;
	}
}

static void close_on_exec(int fd)
{
	fcntl(fd, F_SETFD, FD_CLOEXEC);
}

static int dup_close_on_exec(int old, int new)
{
	if (dup2(old, new) < 0)
		return -1;
	fcntl(new, F_SETFD, FD_CLOEXEC);
	return new;
}

static void handle_child(char **argv, int fd[3], int error_fd)
{
	int i, error, nr_fds = 3;
	int move = error_fd < nr_fds;
	int max = error_fd;

	/*
	 * Find if we must move fds out of the way.
	 */
	for (i = 0; i < nr_fds; i++) {
		if (fd[i] > max)
			max = fd[i];
		if (fd[i] < i)
			move = 1;
	}

	if (move) {
		int next_free = max + 1;

		if (error_fd < nr_fds) {
			error_fd = dup_close_on_exec(error_fd, next_free++);
			if (error_fd < 0)
				goto out;
		}
		for (i = 0; i < nr_fds; i++) {
			if (fd[i] < i) {
				fd[i] = dup_close_on_exec(fd[i], next_free++);
				if (fd[i] < 0)
					goto out;
			}
		}
	}

	/*
	 * Now it is safe to duplicate fds in this order.
	 */
	for (i = 0; i < nr_fds; i++) {
		if (i == fd[i]) {
			// Clear FD_CLOEXEC flag
			fcntl(fd[i], F_SETFD, 0);
		} else {
			if (dup2(fd[i], i) < 0)
				goto out;
		}
	}

	execvp(argv[0], argv);
out:
	error = errno;
	error = write(error_fd, &error, sizeof(error));
	exit(42);
}

static int fork_exec(char **argv, int fd[3])
{
	int pid, rc, status, error = 0;
	int ep[2];

	if (pipe(ep))
		return -1;

	close_on_exec(ep[0]);
	close_on_exec(ep[1]);

	pid = fork();
	if (pid < 0) {
		error = errno;
		close(ep[0]);
		close(ep[1]);
		errno = error;
		return pid;
	}
	if (!pid)
		handle_child(argv, fd, ep[1]);

	close(ep[1]);
	rc = read(ep[0], &error, sizeof(error));
	if (rc > 0 && rc != sizeof(error))
		error = EPIPE;
	if (rc < 0)
		error = errno;
	close(ep[0]);

	if (!rc) {
		// child exec was successful
		return pid;
	}

	while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
		;
	errno = error;
	return -1;
}

int spawn_filter(char **argv, struct filter_data *data)
{
	int p0[2] = { -1, -1 };
	int p1[2] = { -1, -1 };
	int dev_null = -1;
	int fd[3], pid, status;

	data->out = NULL;
	data->out_len = 0;

	if (pipe(p0) || pipe(p1)) {
		error_msg("pipe: %s", strerror(errno));
		goto error;
	}
	dev_null = open("/dev/null", O_WRONLY);
	if (dev_null < 0) {
		error_msg("Error opening /dev/null: %s", strerror(errno));
		goto error;
	}

	close_on_exec(p0[0]);
	close_on_exec(p0[1]);
	close_on_exec(p1[0]);
	close_on_exec(p1[1]);
	close_on_exec(dev_null);

	fd[0] = p0[0];
	fd[1] = p1[1];
	fd[2] = dev_null;
	pid = fork_exec(argv, fd);
	if (pid < 0) {
		error_msg("Error: %s", strerror(errno));
		goto error;
	}

	close(dev_null);
	close(p0[0]);
	close(p1[1]);
	filter(p1[0], p0[1], data);
	close(p1[0]);
	close(p0[1]);

	while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
		;

	return 0;
error:
	close(p0[0]);
	close(p0[1]);
	close(p1[0]);
	close(p1[1]);
	close(dev_null);
	return -1;
}

static struct compiler_format *find_compiler_format(const char *name)
{
	int i;

	for (i = 0; i < nr_compiler_formats; i++) {
		if (!strcmp(compiler_formats[i]->compiler, name))
			return compiler_formats[i];
	}
	return NULL;
}

void spawn(char **args, unsigned int flags, const char *compiler)
{
	struct compiler_format *cf = NULL;
	unsigned int stdout_quiet = flags & (SPAWN_PIPE_STDOUT | SPAWN_REDIRECT_STDOUT);
	unsigned int stderr_quiet = flags & (SPAWN_PIPE_STDERR | SPAWN_REDIRECT_STDERR);
	int quiet = stdout_quiet && stderr_quiet && !compiler;
	int pid, status;
	int p[2];

	if (compiler) {
		cf = find_compiler_format(compiler);
		if (!cf) {
			error_msg("No such error parser %s", compiler);
			return;
		}
	}

	if (flags & (SPAWN_PIPE_STDOUT | SPAWN_PIPE_STDERR) && pipe(p)) {
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
		if (flags & SPAWN_PIPE_STDERR)
			dup2(p[1], 2);
		if (flags & SPAWN_PIPE_STDOUT)
			dup2(p[1], 1);

		// this should be unnecessary but better safe than sorry
		for (i = 3; i < 30; i++)
			close(i);

		execvp(args[0], args);
		exit(42);
	}
	if (cf) {
		close(p[1]);
		read_stderr(cf, p[0], flags);
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

static void goto_file_line(const char *filename, int line, int column)
{
	struct view *v = open_buffer(filename, 0);

	if (!v) {
		return;
	}
	set_view(v);
	move_to_line(line);
	if (column > 0)
		move_to_column(column);
}

void show_compile_error(void)
{
	struct compile_error *e = cerr.errors[cerr.pos];

	if (e->file && e->line > 0) {
		goto_file_line(e->file, e->line, e->column);
	} else if (e->file && cerr.pos + 1 < cerr.count) {
		struct compile_error *next = cerr.errors[cerr.pos + 1];
		if (next->file && next->line > 0)
			goto_file_line(next->file, next->line, next->column);
	}
	info_msg("[%d/%d] %s", cerr.pos + 1, cerr.count, e->msg);
}

static struct compiler_format *add_compiler_format(const char *name)
{
	struct compiler_format *cf = find_compiler_format(name);
	size_t cur_alloc, new_alloc;

	if (cf)
		return cf;

	cur_alloc = (nr_compiler_formats + 3) & ~3;
	new_alloc = (nr_compiler_formats + 4) & ~3;
	if (cur_alloc < new_alloc)
		xrenew(compiler_formats, new_alloc);

	cf = xnew(struct compiler_format, 1);
	cf->compiler = xstrdup(name);
	cf->formats = NULL;
	cf->nr_formats = 0;
	compiler_formats[nr_compiler_formats++] = cf;
	return cf;
}

static struct error_format *add_format(struct compiler_format *cf)
{
	size_t cur_alloc, new_alloc;

	cur_alloc = (cf->nr_formats + 3) & ~3;
	new_alloc = (cf->nr_formats + 4) & ~3;
	if (cur_alloc < new_alloc)
		xrenew(cf->formats, new_alloc);
	return &cf->formats[cf->nr_formats++];
}

void add_error_fmt(const char *compiler, enum msg_importance importance, const char *format, char **desc)
{
	const char *names[] = { "file", "line", "column", "message" };
	int idx[ARRAY_COUNT(names)] = { -1, -1, -1, 0 };
	struct error_format *p;
	int i, j;

	for (i = 0; desc[i]; i++) {
		for (j = 0; j < ARRAY_COUNT(names); j++) {
			if (!strcmp(desc[i], names[j])) {
				idx[j] = i + 1;
				break;
			}
		}
		if (j == ARRAY_COUNT(names)) {
			error_msg("Unknown substring name %s.", desc[i]);
			return;
		}
	}

	p = add_format(add_compiler_format(compiler));
	p->importance = importance;
	p->msg_idx = idx[3];
	p->file_idx = idx[0];
	p->line_idx = idx[1];
	p->column_idx = idx[2];
	p->pattern = xstrdup(format);
}
