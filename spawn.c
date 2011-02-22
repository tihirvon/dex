#include "spawn.h"
#include "editor.h"
#include "buffer.h"
#include "window.h"
#include "move.h"
#include "regexp.h"
#include "gbuf.h"
#include "msg.h"

static PTR_ARRAY(compilers);

static char *format_msg(const char *msg)
{
	int i, len = strlen(msg);
	char *buf = xnew(char, len + 1);

	for (i = 0; i <= len; i++)
		buf[i] = msg[i] == '\t' ? ' ' : msg[i];
	return buf;
}

static void handle_error_msg(struct compiler *c, char *str, unsigned int flags)
{
	const struct error_format *p;
	char *nl = strchr(str, '\n');
	int i, len;

	if (nl)
		*nl = 0;
	len = strlen(str);
	if (flags & SPAWN_PRINT_ERRORS)
		fprintf(stderr, "%s\n", str);

	for (i = 0; str[i]; i++) {
		if (str[i] == '\t')
			str[i] = ' ';
	}
	for (i = 0; ; i++) {
		if (i == c->error_formats.count) {
			char *s = format_msg(str);
			add_message(new_message(s));
			free(s);
			return;
		}
		p = c->error_formats.ptrs[i];
		if (regexp_match(p->pattern, str, len))
			break;
	}

	if (!p->ignore) {
		struct message *m = new_message(regexp_matches[p->msg_idx]);
		m->file = p->file_idx < 0 ? NULL : xstrdup(regexp_matches[p->file_idx]);
		m->u.location.line = p->line_idx < 0 ? 0 : atoi(regexp_matches[p->line_idx]);
		m->u.location.column= p->column_idx < 0 ? 0 : atoi(regexp_matches[p->column_idx]);
		add_message(m);
	}
	free_regexp_matches();
}

static void read_errors(struct compiler *c, int fd, unsigned int flags)
{
	FILE *f = fdopen(fd, "r");
	char line[4096];

	if (!f) {
		// should not happen
		return;
	}

	while (fgets(line, sizeof(line), f))
		handle_error_msg(c, line, flags);
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

	/*
	 * Unignore signals. See man page exec(3p) for more information.
	 */
	set_signal_handler(SIGINT, SIG_DFL);
	set_signal_handler(SIGQUIT, SIG_DFL);

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

struct compiler *find_compiler(const char *name)
{
	int i;

	for (i = 0; i < compilers.count; i++) {
		struct compiler *c = compilers.ptrs[i];
		if (!strcmp(c->name, name))
			return c;
	}
	return NULL;
}

void spawn_compiler(char **args, unsigned int flags, struct compiler *c)
{
	unsigned int redir = SPAWN_REDIRECT_STDOUT | SPAWN_REDIRECT_STDERR;
	int pid, status, quiet;
	int dev_null, p[2], fd[3];

	dev_null = open("/dev/null", O_WRONLY);
	if (dev_null < 0) {
		error_msg("Error opening /dev/null: %s", strerror(errno));
		return;
	}
	if (pipe(p)) {
		error_msg("pipe: %s", strerror(errno));
		close(dev_null);
		return;
	}

	fd[0] = dev_null;
	if (flags & SPAWN_READ_STDOUT) {
		fd[1] = p[1];
		fd[2] = 2;
	} else {
		fd[1] = 1;
		fd[2] = p[1];
	}

	flags |= SPAWN_PRINT_ERRORS;
	if (flags & SPAWN_REDIRECT_STDOUT) {
		if (fd[1] == p[1])
			flags &= ~SPAWN_PRINT_ERRORS;
		else
			fd[1] = dev_null;
	}
	if (flags & SPAWN_REDIRECT_STDERR) {
		if (fd[2] == p[1])
			flags &= ~SPAWN_PRINT_ERRORS;
		else
			fd[2] = dev_null;
	}

	quiet = !(flags & SPAWN_PRINT_ERRORS) && (flags & redir) == redir;
	if (!quiet) {
		child_controls_terminal = 1;
		ui_end();
	}

	close_on_exec(p[0]);
	close_on_exec(p[1]);
	close_on_exec(dev_null);
	pid = fork_exec(args, fd);
	if (pid < 0) {
		error_msg("Error: %s", strerror(errno));
		if (!quiet)
			ui_start(0);
		close(p[0]);
		close(p[1]);
		close(dev_null);
	} else {
		close(dev_null);
		close(p[1]);
		read_errors(c, p[0], flags);
		close(p[0]);
		while (wait(&status) < 0 && errno == EINTR)
			;
		if (!quiet) {
			ui_start(flags & SPAWN_PROMPT);
			child_controls_terminal = 0;
		}
	}
}

void spawn(char **args, int fd[3], int prompt)
{
	int i, pid, status, quiet, redir_count = 0;
	int dev_null = -1;

	for (i = 0; i < 3; i++) {
		if (fd[i] >= 0)
			continue;

		if (dev_null < 0) {
			dev_null = open("/dev/null", O_WRONLY);
			if (dev_null < 0) {
				error_msg("Error opening /dev/null: %s", strerror(errno));
				return;
			}
			close_on_exec(dev_null);
		}
		fd[i] = dev_null;
		redir_count++;
	}
	quiet = redir_count == 3;

	if (!quiet) {
		child_controls_terminal = 1;
		ui_end();
	}

	pid = fork_exec(args, fd);
	if (pid < 0) {
		error_msg("Error: %s", strerror(errno));
		if (!quiet)
			ui_start(0);
		close(dev_null);
		return;
	}

	close(dev_null);
	while (wait(&status) < 0 && errno == EINTR)
		;
	if (!quiet) {
		ui_start(prompt);
		child_controls_terminal = 0;
	}
}

static struct compiler *add_compiler(const char *name)
{
	struct compiler *c = find_compiler(name);

	if (c)
		return c;

	c = xnew0(struct compiler, 1);
	c->name = xstrdup(name);
	ptr_array_add(&compilers, c);
	return c;
}

void add_error_fmt(const char *compiler, int ignore, const char *format, char **desc)
{
	const char *names[] = { "file", "line", "column", "message" };
	int idx[ARRAY_COUNT(names)] = { -1, -1, -1, 0 };
	struct error_format *f;
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

	f = xnew0(struct error_format, 1);
	f->ignore = ignore;
	f->msg_idx = idx[3];
	f->file_idx = idx[0];
	f->line_idx = idx[1];
	f->column_idx = idx[2];
	f->pattern = xstrdup(format);

	ptr_array_add(&add_compiler(compiler)->error_formats, f);
}
