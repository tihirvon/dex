#include "util.h"

char *home_dir;

void init_misc(void)
{
	home_dir = getenv("HOME");
	if (!home_dir) {
		home_dir = xcalloc(1);
	} else {
		home_dir = xstrdup(home_dir);
	}
}

const char *editor_file(const char *name)
{
	static char filename[1024];
	snprintf(filename, sizeof(filename), "%s/.editor/%s", home_dir, name);
	return filename;
}

unsigned int count_nl(const char *buf, unsigned int size)
{
	unsigned int i, nl = 0;
	for (i = 0; i < size; i++) {
		if (buf[i] == '\n')
			nl++;
	}
	return nl;
}

unsigned int copy_count_nl(char *dst, const char *src, unsigned int len)
{
	unsigned int i, nl = 0;
	for (i = 0; i < len; i++) {
		dst[i] = src[i];
		if (src[i] == '\n')
			nl++;
	}
	return nl;
}

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

int wbuf_flush(struct wbuf *wbuf)
{
	if (wbuf->fill) {
		ssize_t rc = xwrite(wbuf->fd, wbuf->buf, wbuf->fill);
		if (rc < 0)
			return rc;
	}
	return 0;
}

int wbuf_write_str(struct wbuf *wbuf, const char *str)
{
	int len = strlen(str);
	ssize_t rc;

	if (wbuf->fill + len > sizeof(wbuf->buf)) {
		rc = wbuf_flush(wbuf);
		if (rc < 0)
			return rc;
	}
	if (len >= sizeof(wbuf->buf)) {
		rc = wbuf_flush(wbuf);
		if (rc < 0)
			return rc;
		rc = xwrite(wbuf->fd, str, len);
		if (rc < 0)
			return rc;
		return 0;
	}
	memcpy(wbuf->buf + wbuf->fill, str, len);
	wbuf->fill += len;
	return 0;
}

int wbuf_write_ch(struct wbuf *wbuf, char ch)
{
	if (wbuf->fill + 1 > sizeof(wbuf->buf)) {
		ssize_t rc = wbuf_flush(wbuf);
		if (rc < 0)
			return rc;
	}
	wbuf->buf[wbuf->fill++] = ch;
	return 0;
}

void bug(const char *function, const char *fmt, ...)
{
	va_list ap;

	ui_end();

	fprintf(stderr, "%s: ", function);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	exit(42);
}

void debug_print(const char *function, const char *fmt, ...)
{
	va_list ap;

	fprintf(stderr, "%s: ", function);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
}

static int remove_double_slashes(char *str)
{
	char prev = 0;
	int s, d;

	d = 0;
	for (s = 0; str[s]; s++) {
		char ch = str[s];

		if (ch == '/' && prev == '/')
			continue;
		str[d++] = ch;
		prev = ch;
	}
	str[d] = 0;
	return d;
}

/*
 * canonicalizes filename
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
		getcwd(buf, sizeof(buf));
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

		if (last) {
			if (!S_ISREG(st.st_mode)) {
				// FIXME: better error message
				errno = EBADF;
				return NULL;
			}
			break;
		}

		if (!S_ISDIR(st.st_mode)) {
			errno = ENOTDIR;
			return NULL;
		}

		*ep = '/';
		sp = ep + 1;
	}
	return xstrdup(buf);
}
