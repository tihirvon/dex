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
		fd = open(editor_file("debug.log"), O_WRONLY | O_CREAT | O_APPEND, 0666);
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

static int make_absolute(char *dst, const char *src)
{
	int len = strlen(src);
	int pos = 0;

	if (src[0] != '/') {
		if (!getcwd(dst, PATH_MAX - 1))
			return 0;
		pos = strlen(dst);
		dst[pos++] = '/';
	}
	if (pos + len + 1 > PATH_MAX) {
		errno = ENAMETOOLONG;
		return 0;
	}
	memcpy(dst + pos, src, len + 1);
	return 1;
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
	char *sp;

	if (!make_absolute(buf, filename))
		return NULL;

	remove_double_slashes(buf);

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
			continue;
		}
		if (!strcmp(sp, "..")) {
			if (sp != buf + 1) {
				// not first component, remove previous component
				sp--;
				while (sp[-1] != '/')
					sp--;
			}

			if (last) {
				*sp = 0;
				break;
			}
			memmove(sp, ep + 1, strlen(ep + 1) + 1);
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
			char tmp[PATH_MAX];
			int target_len;
			int total_len = 0;
			int buf_len = sp - 1 - buf;
			int rest_len = 0;
			int pos = 0;
			const char *rest = NULL;

			if (!last) {
				rest = ep + 1;
				rest_len = strlen(rest);
			}
			if (++depth > 8) {
				errno = ELOOP;
				return NULL;
			}
			target_len = readlink(buf, target, sizeof(target));
			if (target_len < 0)
				return NULL;
			if (target_len == sizeof(target)) {
				errno = ENAMETOOLONG;
				return NULL;
			}
			target[target_len] = 0;

			// calculate length
			if (target[0] != '/')
				total_len = buf_len + 1;
			total_len += target_len;
			if (rest)
				total_len += 1 + rest_len;
			if (total_len >= PATH_MAX) {
				errno = ENAMETOOLONG;
				return NULL;
			}

			// build new path
			if (target[0] != '/') {
				memcpy(tmp, buf, buf_len);
				pos += buf_len;
				tmp[pos++] = '/';
			}
			memcpy(tmp + pos, target, target_len);
			pos += target_len;
			if (rest) {
				tmp[pos++] = '/';
				memcpy(tmp + pos, rest, rest_len);
				pos += rest_len;
			}
			tmp[pos] = 0;
			pos = remove_double_slashes(tmp);

			// restart
			memcpy(buf, tmp, pos + 1);
			sp = buf + 1;
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
