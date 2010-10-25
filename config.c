#include "config.h"
#include "util.h"
#include "editor.h"
#include "command.h"
#include "gbuf.h"

const char *config_file;
int config_line;

static int is_command(const char *str, int len)
{
	int i;
	for (i = 0; i < len; i++) {
		if (str[i] == '#')
			return 0;
		if (!isspace(str[i]))
			return 1;
	}
	return 0;
}

int do_read_config(const struct command *cmds, const char *filename, int must_exist)
{
	struct stat st;
	size_t size;
	GBUF(line);
	char *buf, *ptr;
	int fd;

	fd = open(filename, O_RDONLY);
	if (fd < 0) {
		if (errno != ENOENT || must_exist)
			error_msg("Error opening %s: %s", filename, strerror(errno));
		return -1;
	}
	fstat(fd, &st);
	size = st.st_size;
	buf = xmmap(fd, 0, size);
	close(fd);
	if (!buf) {
		error_msg("mmap failed for %s: %s", filename, strerror(errno));
		return -1;
	}

	config_file = filename;
	config_line = 1;

	ptr = buf;
	while (ptr < buf + size) {
		size_t n = buf + size - ptr;
		char *end = memchr(ptr, '\n', n);

		if (end)
			n = end - ptr;

		if (line.len || is_command(ptr, n)) {
			if (n && ptr[n - 1] == '\\') {
				gbuf_add_buf(&line, ptr, n - 1);
			} else {
				gbuf_add_buf(&line, ptr, n);
				handle_command(cmds, line.buffer);
				gbuf_clear(&line);
			}
		}
		config_line++;
		ptr += n + 1;
	}
	if (line.len)
		handle_command(cmds, line.buffer);
	gbuf_free(&line);
	xmunmap(buf, st.st_size);
	return 0;
}

int read_config(const struct command *cmds, const char *filename, int must_exist)
{
	/* recursive */
	const char *saved_config_file = config_file;
	int saved_config_line = config_line;
	int ret = do_read_config(cmds, filename, must_exist);
	config_file = saved_config_file;
	config_line = saved_config_line;
	return ret;
}
