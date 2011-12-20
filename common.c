#include "common.h"
#include "editor.h"

#include <sys/mman.h>

const char hex_tab[16] = "0123456789abcdef";
int term_utf8;

int count_strings(char **strings)
{
	int count;

	for (count = 0; strings[count]; count++)
		;
	return count;
}

unsigned int number_width(unsigned int n)
{
	unsigned int width = 0;

	do {
		n /= 10;
		width++;
	} while (n);
	return width;
}

const char *ssprintf(const char *format, ...)
{
	static char buf[1024];
	va_list ap;
	va_start(ap, format);
	vsnprintf(buf, sizeof(buf), format, ap);
	va_end(ap);
	return buf;
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

// Returns size of file or -1 on error.
// For empty file *bufp is NULL otherwise *bufp is NUL-terminated.
ssize_t read_file(const char *filename, char **bufp)
{
	struct stat st;
	char *buf;
	ssize_t r;
	int fd = open(filename, O_RDONLY);

	*bufp = NULL;
	if (fd == -1)
		return -1;
	if (fstat(fd, &st) == -1) {
		close(fd);
		return -1;
	}
	buf = xnew(char, st.st_size + 1);
	r = xread(fd, buf, st.st_size);
	close(fd);
	if (r > 0) {
		buf[r] = 0;
		*bufp = buf;
	} else {
		free(buf);
	}
	return r;
}

char *buf_next_line(char *buf, ssize_t *posp, ssize_t size)
{
	ssize_t pos = *posp;
	ssize_t avail = size - pos;
	char *line = buf + pos;
	char *nl = memchr(line, '\n', avail);
	if (nl) {
		*nl = 0;
		*posp += nl - line + 1;
	} else {
		line[avail] = 0;
		*posp += avail;
	}
	return line;
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
