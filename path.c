#include "editor.h"
#include "path.h"
#include "common.h"

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

static char *relative_filename(const char *f, const char *cwd)
{
	int i, tpos, tlen, dotdot, len, clen = 0;

	// length of common path
	while (cwd[clen] && cwd[clen] == f[clen])
		clen++;

	if (!cwd[clen] && f[clen] == '/') {
		// cwd    = /home/user
		// abs    = /home/user/project-a/file.c
		// common = /home/user
		return xstrdup(f + clen + 1);
	}

	// cwd    = /home/user/src/project
	// abs    = /home/user/save/parse.c
	// common = /home/user/s
	// find "save/parse.c"
	tpos = clen;
	while (tpos && f[tpos] != '/')
		tpos--;
	if (f[tpos] == '/')
		tpos++;

	// number of "../" needed
	dotdot = 1;
	for (i = clen + 1; cwd[i]; i++) {
		if (cwd[i] == '/')
			dotdot++;
	}

	tlen = strlen(f + tpos);
	len = dotdot * 3 + tlen;
	if (dotdot < 3 && len < strlen(f)) {
		char *filename = xnew(char, len + 1);
		for (i = 0; i < dotdot; i++)
			memcpy(filename + i * 3, "../", 3);
		memcpy(filename + dotdot * 3, f + tpos, tlen + 1);
		return filename;
	}
	return NULL;
}

char *short_filename_cwd(const char *absolute, const char *cwd)
{
	int home_len = strlen(home_dir);
	char *rel = relative_filename(absolute, cwd);

	if (!memcmp(absolute, home_dir, home_len) && absolute[home_len] == '/') {
		int abs_len = strlen(absolute);
		int len = abs_len - home_len + 1;
		if (!rel || len < strlen(rel)) {
			char *filename = xnew(char, len + 1);
			filename[0] = '~';
			memcpy(filename + 1, absolute + home_len, len);
			free(rel);
			return filename;
		}
	}
	if (rel)
		return rel;
	return xstrdup(absolute);
}

char *short_filename(const char *absolute)
{
	char cwd[PATH_MAX];

	if (getcwd(cwd, sizeof(cwd)))
		return short_filename_cwd(absolute, cwd);
	return xstrdup(absolute);
}

const char *filename_to_utf8(const char *filename)
{
	static char buf[4096];
	size_t ic, oc, ocsave;
	char *ib, *ob;
	iconv_t cd;

	cd = iconv_open("UTF-8", charset);
	if (cd == (iconv_t)-1) {
		d_print("iconv_open() using charset %s failed: %s\n", charset, strerror(errno));
		return filename;
	}

	ib = (char *)filename;
	ic = strlen(filename);
	ob = buf;
	oc = sizeof(buf) - 1;

	ocsave = oc;
	while (ic > 0) {
		size_t rc = iconv(cd, (void *)&ib, &ic, &ob, &oc);
		if (rc == (size_t)-1) {
			switch (errno) {
			case EILSEQ:
			case EINVAL:
				// can't convert this byte
				ob[0] = ib[0];
				ic--;
				oc--;

				// reset
				iconv(cd, NULL, NULL, NULL, NULL);
				break;
			case E2BIG:
			default:
				// FIXME
				ic = 0;
			}
		}
	}
	iconv_close(cd);

	buf[ocsave - oc] = 0;
	return buf;
}
