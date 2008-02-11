#include "buffer.h"

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
