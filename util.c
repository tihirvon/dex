#include "util.h"
#include "editor.h"

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
		wbuf->fill = 0;
	}
	return 0;
}

int wbuf_write(struct wbuf *wbuf, const char *buf, size_t count)
{
	ssize_t rc;

	if (wbuf->fill + count > sizeof(wbuf->buf)) {
		rc = wbuf_flush(wbuf);
		if (rc < 0)
			return rc;
	}
	if (count >= sizeof(wbuf->buf)) {
		rc = wbuf_flush(wbuf);
		if (rc < 0)
			return rc;
		rc = xwrite(wbuf->fd, buf, count);
		if (rc < 0)
			return rc;
		return 0;
	}
	memcpy(wbuf->buf + wbuf->fill, buf, count);
	wbuf->fill += count;
	return 0;
}

int wbuf_write_str(struct wbuf *wbuf, const char *str)
{
	return wbuf_write(wbuf, str, strlen(str));
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

int regexp_match_nosub(const char *pattern, const char *str)
{
	regex_t re;
	int rc;

	rc = regcomp(&re, pattern, REG_EXTENDED | REG_NEWLINE | REG_NOSUB);
	if (rc) {
		regfree(&re);
		return 0;
	}
	rc = regexec(&re, str, 0, NULL, 0);
	regfree(&re);
	return !rc;
}

#define REGEXP_SUBSTRINGS 8

char *regexp_matches[REGEXP_SUBSTRINGS + 1];

int regexp_match(const char *pattern, const char *str)
{
	regmatch_t m[REGEXP_SUBSTRINGS];
	regex_t re;
	int err, ret;

	err = regcomp(&re, pattern, REG_EXTENDED | REG_NEWLINE);
	if (err) {
		regfree(&re);
		return 0;
	}
	ret = !regexec(&re, str, REGEXP_SUBSTRINGS, m, 0);
	regfree(&re);
	if (ret) {
		int i;
		for (i = 0; i < REGEXP_SUBSTRINGS; i++) {
			if (m[i].rm_so == -1)
				break;
			regexp_matches[i] = xstrndup(str + m[i].rm_so, m[i].rm_eo - m[i].rm_so);
		}
		regexp_matches[i] = NULL;
	}
	return ret;
}

void free_regexp_matches(void)
{
	int i;

	for (i = 0; i < REGEXP_SUBSTRINGS; i++) {
		free(regexp_matches[i]);
		regexp_matches[i] = NULL;
	}
}

int buf_regexec(const regex_t *regexp, const char *buf,
	unsigned int size, size_t nr_m, regmatch_t *m, int flags)
{
#ifdef REG_STARTEND
	m[0].rm_so = 0;
	m[0].rm_eo = size;
	return regexec(regexp, buf, nr_m, m, flags | REG_STARTEND);
#else
	// buffer must be null-terminated string if REG_STARTED is not supported
	char *tmp = xnew(char, size + 1);
	int ret;

	memcpy(tmp, buf, size);
	tmp[size] = 0;
	ret = regexec(regexp, tmp, nr_m, m, flags);
	free(tmp);
	return ret;
#endif
}
