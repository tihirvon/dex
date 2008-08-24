#include "util.h"
#include "commands.h"

const char *config_file;
int config_line;

int read_config(const char *filename)
{
	/* recursive */
	const char *saved_config_file = config_file;
	int saved_config_line = config_line;

	struct stat st;
	size_t size, alloc = 0;
	char *buf, *ptr, *line = NULL;
	int fd;
	int ret = -1;

	fd = open(filename, O_RDONLY);
	if (fd < 0) {
		goto out;
	}
	fstat(fd, &st);
	size = st.st_size;
	buf = xmmap(fd, 0, size);
	close(fd);
	if (!buf) {
		goto out;
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
	ret = 0;
out:
	config_file = saved_config_file;
	config_line = saved_config_line;
	return ret;
}
