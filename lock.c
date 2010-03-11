#include "lock.h"
#include "buffer.h"
#include "editor.h"
#include "util.h"

static char *file_locks;
static char *file_locks_lock;

static int process_exists(int pid)
{
	return !kill(pid, 0);
}

static int lock_or_unlock(const char *filename, int lock)
{
	int tries = 0;
	int wfd, rfd, filename_len;
	ssize_t size;
	char *buf = NULL, *ptr, *end;

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

	filename_len = strlen(filename);
	rfd = open(file_locks, O_RDONLY);
	if (rfd < 0) {
		if (errno != ENOENT) {
			error_msg("Error opening %s: %s", file_locks, strerror(errno));
			goto error;
		}
		size = 0;
		buf = xmalloc(filename_len + 32);
	} else {
		struct stat st;

		fstat(rfd, &st);
		size = st.st_size;
		buf = xmalloc(size + filename_len + 32);

		if (size > 0)
			size = xread(rfd, buf, size);
		close(rfd);
		if (size < 0) {
			error_msg("Error reading %s: %s", file_locks, strerror(errno));
			goto error;
		}
	}

	ptr = buf;
	end = buf + size;
	while (ptr < end) {
		char *bol = ptr;
		char *lf;
		int pid = 0;

		while (ptr < end && isdigit(*ptr)) {
			pid *= 10;
			pid += *ptr++ - '0';
		}
		while (ptr < end && (*ptr == ' ' || *ptr == '\t'))
			ptr++;
		lf = memchr(ptr, '\n', end - ptr);
		if (!lf) {
			lf = end;
			*end++ = '\n';
			size++;
		}
		if (filename_len == lf - ptr && !memcmp(ptr, filename, filename_len)) {
			if (lock && process_exists(pid)) {
				error_msg("File is locked (%s) by process %d", file_locks, pid);
				goto error;
			}
			if (lock)
				error_msg("Releasing lock from dead process %d", pid);
			memmove(bol, lf + 1, end - lf - 1);
			size -= lf + 1 - bol;
			break;
		}
		ptr = lf + 1;
	}
	if (lock) {
		sprintf(buf + size, "%d %s\n", getpid(), filename);
		size += strlen(buf + size);
	}
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
	return 0;
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
