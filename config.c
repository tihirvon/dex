#include "util.h"
#include "commands.h"

const char *config_file;
int config_line;

void read_config(void)
{
	const char *filename;
	struct stat st;
	size_t size, alloc = 0;
	char *buf, *ptr, *line = NULL;
	int fd;

	filename = editor_file("rc");
	fd = open(filename, O_RDONLY);
	if (fd < 0) {
		return;
	}
	fstat(fd, &st);
	size = st.st_size;
	buf = xmmap(fd, 0, size);
	close(fd);
	if (!buf) {
		return;
	}

	config_file = filename;
	config_line = 1;

	ptr = buf;
	while (ptr < buf + size) {
		size_t n = buf + size - ptr;
		char *end = memchr(ptr, '\n', n);

		if (end)
			n = end - ptr;

		if (alloc < n + 1) {
			alloc = ROUND_UP(n + 1, 64);
			xrenew(line, alloc);
		}
		memcpy(line, ptr, n);
		line[n] = 0;
		handle_command(line);
		config_line++;

		ptr += n + 1;
	}
	free(line);
	xmunmap(buf, st.st_size);

	config_file = NULL;
	config_line = 0;
}
