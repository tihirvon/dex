#include "lock.h"
#include "buffer.h"
#include "editor.h"

static char *file_locks;
static char *file_locks_lock;

static int process_exists(int pid)
{
	return !kill(pid, 0);
}

static int rewrite_lock_file(char *buf, ssize_t *sizep, const char *filename, int lock)
{
	int filename_len = strlen(filename);
	int my_pid = getpid();
	int err = 0;
	ssize_t size = *sizep;
	ssize_t pos = 0;

	while (pos < size) {
		ssize_t next_bol, bol = pos;
		int same, remove_line = 0;
		int pid = 0;
		char *nl;

		while (pos < size && isdigit(buf[pos])) {
			pid *= 10;
			pid += buf[pos++] - '0';
		}
		while (pos < size && (buf[pos] == ' ' || buf[pos] == '\t'))
			pos++;
		nl = memchr(buf + pos, '\n', size - pos);
		next_bol = nl - buf + 1;

		same = filename_len == next_bol - 1 - pos && !memcmp(buf + pos, filename, filename_len);
		if (pid == my_pid) {
			if (same) {
				// lock = 1 => pid conflict. lock must be stale
				// lock = 0 => normal unlock case
				remove_line = 1;
			}
		} else if (process_exists(pid)) {
			if (same && lock) {
				error_msg("File is locked (%s) by process %d", file_locks, pid);
				err = -1;
			}
		} else {
			// release lock from dead process
			remove_line = 1;
		}

		if (remove_line) {
			memmove(buf + bol, buf + next_bol, size - next_bol);
			size -= next_bol - bol;
			pos = bol;
		} else {
			pos = next_bol;
		}
	}
	if (lock && err == 0) {
		sprintf(buf + size, "%d %s\n", my_pid, filename);
		size += strlen(buf + size);
	}
	*sizep = size;
	return err;
}

static int lock_or_unlock(const char *filename, int lock)
{
	int tries = 0;
	int wfd, rfd, err;
	ssize_t size;
	char *buf = NULL;

	if (!file_locks) {
		file_locks = xstrdup(editor_file("file-locks"));
		file_locks_lock = xstrdup(editor_file("file-locks.lock"));
	}
	if (!strcmp(filename, file_locks) || !strcmp(filename, file_locks_lock))
		return 0;

	while (1) {
		wfd = open(file_locks_lock, O_WRONLY | O_CREAT | O_EXCL, 0666);
		if (wfd >= 0)
			break;

		if (errno != EEXIST) {
			error_msg("Error creating %s: %s", file_locks_lock, strerror(errno));
			return -1;
		}
		if (++tries == 3) {
			if (unlink(file_locks_lock)) {
				error_msg("Error removing stale lock file %s: %s",
						file_locks_lock, strerror(errno));
				return -1;
			}
			error_msg("Stale lock file %s removed.", file_locks_lock);
		} else {
			struct timespec req = {
				.tv_sec = 0,
				.tv_nsec = 100 * 1000000,
			};

			nanosleep(&req, NULL);
		}
	}

	rfd = open(file_locks, O_RDONLY);
	if (rfd < 0) {
		if (errno != ENOENT) {
			error_msg("Error opening %s: %s", file_locks, strerror(errno));
			goto error;
		}
		size = 0;
		buf = xmalloc(strlen(filename) + 32);
	} else {
		struct stat st;

		fstat(rfd, &st);
		size = st.st_size;
		buf = xmalloc(size + strlen(filename) + 32);

		if (size > 0)
			size = xread(rfd, buf, size);
		close(rfd);
		if (size < 0) {
			error_msg("Error reading %s: %s", file_locks, strerror(errno));
			goto error;
		}
	}

	if (size && buf[size - 1] != '\n')
		buf[size++] = '\n';

	err = rewrite_lock_file(buf, &size, filename, lock);
	if (xwrite(wfd, buf, size) < 0) {
		error_msg("Error writing %s: %s", file_locks_lock, strerror(errno));
		goto error;
	}
	if (rename(file_locks_lock, file_locks)) {
		error_msg("Renaming %s to %s: %s", file_locks_lock, file_locks, strerror(errno));
		goto error;
	}
	free(buf);
	close(wfd);
	return err;
error:
	unlink(file_locks_lock);
	free(buf);
	close(wfd);
	return -1;
}

int lock_file(const char *filename)
{
	return lock_or_unlock(filename, 1);
}

void unlock_file(const char *filename)
{
	lock_or_unlock(filename, 0);
}
