#include "util.h"
#include "editor.h"

#include <sys/mman.h>

ssize_t xread(int fd, void *buf, size_t count)
{
	char *b = buf;
	size_t pos = 0;

	do {
		int rc;

		rc = read(fd, b + pos, count - pos);
		if (rc == -1) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (rc == 0) {
			/* eof */
			break;
		}
		pos += rc;
	} while (count - pos > 0);
	return pos;
}

ssize_t xwrite(int fd, const void *buf, size_t count)
{
	const char *b = buf;
	size_t count_save = count;

	do {
		int rc;

		rc = write(fd, b, count);
		if (rc == -1) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		b += rc;
		count -= rc;
	} while (count > 0);
	return count_save;
}

void bug(const char *function, const char *fmt, ...)
{
	va_list ap;

	ui_end();

	fprintf(stderr, "\n *** BUG *** %s: ", function);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);

	// for core dump
	abort();
}

void debug_print(const char *function, const char *fmt, ...)
{
	static int fd = -1;
	char buf[4096];
	int pos;
	va_list ap;

	if (fd < 0) {
		fd = open("/tmp/editor.log", O_WRONLY | O_CREAT | O_APPEND, 0666);
		BUG_ON(fd < 0);

		// don't leak file descriptor to parent processes
		fcntl(fd, F_SETFD, FD_CLOEXEC);
	}

	snprintf(buf, sizeof(buf), "%s: ", function);
	pos = strlen(buf);
	va_start(ap, fmt);
	vsnprintf(buf + pos, sizeof(buf) - pos, fmt, ap);
	va_end(ap);
	xwrite(fd, buf, strlen(buf));
}

static int remove_double_slashes(char *str)
{
	int s, d = 0;

	for (s = 0; str[s]; s++) {
		if (str[s] != '/' || str[s + 1] != '/')
			str[d++] = str[s];
	}
	str[d] = 0;
	return d;
}

/*
 * canonicalizes path name
 *
 *   - replaces double-slashes with one slash
 *   - removes any "." or ".." path components
 *   - makes path absolute
 *   - expands symbolic links
 *   - checks that all but the last expanded path component are directories
 *   - last path component is allowed to not exist
 */
char *path_absolute(const char *filename)
{
	int depth = 0;
	char buf[PATH_MAX];
	char prev = 0;
	char *sp;
	int s, d;

	d = 0;
	if (filename[0] != '/') {
		if (!getcwd(buf, sizeof(buf)))
			return NULL;
		d = strlen(buf);
		buf[d++] = '/';
		prev = '/';
	}
	for (s = 0; filename[s]; s++) {
		char ch = filename[s];

		if (prev == '/' && ch == '/')
			continue;
		buf[d++] = ch;
		prev = ch;
	}
	buf[d] = 0;

	// for each component:
	//     remove "."
	//     remove ".." and previous component
	//     if symlink then replace with link destination and start over

	sp = buf + 1;
	while (*sp) {
		struct stat st;
		char *ep = strchr(sp, '/');
		int last = !ep;
		int rc;

		if (ep)
			*ep = 0;
		if (!strcmp(sp, ".")) {
			if (last) {
				*sp = 0;
				break;
			}
			memmove(sp, ep + 1, strlen(ep + 1) + 1);
			d_print("'%s' '%s' (.)\n", buf, sp);
			continue;
		}
		if (!strcmp(sp, "..")) {
			if (sp == buf + 1) {
				// first component is "..". remove it
				if (last) {
					*sp = 0;
					break;
				}
				memmove(sp, ep + 1, strlen(ep + 1) + 1);
			} else {
				// remove previous component
				sp -= 2;
				while (*sp != '/')
					sp--;
				sp++;

				if (last) {
					*sp = 0;
					break;
				}
				memmove(sp, ep + 1, strlen(ep + 1) + 1);
			}
			d_print("'%s' '%s' (..)\n", buf, sp);
			continue;
		}

		rc = lstat(buf, &st);
		if (rc) {
			if (last && errno == ENOENT)
				break;
			return NULL;
		}

		if (S_ISLNK(st.st_mode)) {
			char target[PATH_MAX];
			ssize_t len, clen;

			if (++depth > 8) {
				errno = ELOOP;
				return NULL;
			}
			len = readlink(buf, target, sizeof(target));
			if (len < 0) {
				d_print("readlink failed for '%s'\n", buf);
				return NULL;
			}
			if (len == sizeof(target)) {
				errno = ENAMETOOLONG;
				return NULL;
			}
			target[len] = 0;
			len = remove_double_slashes(target);

			if (target[0] == '/')
				sp = buf;

			if (last) {
				if (sp - buf + len + 1 > sizeof(buf)) {
					errno = ENAMETOOLONG;
					return NULL;
				}
				memcpy(sp, target, len + 1);
				d_print("'%s' '%s' (last)\n", buf, sp);
				continue;
			}

			// remove trailing slash
			if (target[len - 1] == '/')
				target[--len] = 0;

			// replace sp - ep with target
			*ep = '/';
			clen = ep - sp;
			if (clen != len) {
				if (len > clen && strlen(buf) + len - clen + 1 > sizeof(buf)) {
					errno = ENAMETOOLONG;
					return NULL;
				}
				memmove(sp + len, ep, strlen(ep) + 1);
			}
			memcpy(sp, target, len);
			d_print("'%s' '%s'\n", buf, sp);
			continue;
		}

		if (last)
			break;

		if (!S_ISDIR(st.st_mode)) {
			errno = ENOTDIR;
			return NULL;
		}

		*ep = '/';
		sp = ep + 1;
	}
	return xstrdup(buf);
}

const char *get_file_type(mode_t mode)
{
	if (S_ISREG(mode))
		return "file";
	if (S_ISDIR(mode))
		return "directory";
	if (S_ISCHR(mode))
		return "character device";
	if (S_ISBLK(mode))
		return "block device";
	if (S_ISFIFO(mode))
		return "named pipe";
	if (S_ISLNK(mode))
		return "symbolic link";
#ifdef S_ISSOCK
	if (S_ISSOCK(mode))
		return "socket";
#endif
	return "unknown";
}

#define mmap_empty ((void *)8UL)

void *xmmap(int fd, off_t offset, size_t len)
{
	void *buf;
	if (!len)
		return mmap_empty;
	buf = mmap(NULL, len, PROT_READ, MAP_PRIVATE, fd, offset);
	if (buf == MAP_FAILED)
		return NULL;
	return buf;
}

void xmunmap(void *start, size_t len)
{
	if (start != mmap_empty) {
		BUG_ON(munmap(start, len));
	} else {
		BUG_ON(len);
	}
}
