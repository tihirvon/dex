#include "buffer.h"
#include "window.h"
#include "search.h"

struct tag_file {
	struct stat st;
	int fd;
	char *map;
};

struct tag_address {
	char *filename;
	char *pattern;
	int line;
};

struct file_location {
	struct list_head node;
	char *filename;
	int line;
};

static struct tag_file *tag_file;
static LIST_HEAD(location_head);

static struct tag_file *open_tag_file(const char *filename)
{
	struct tag_file *tf = xnew(struct tag_file, 1);

	tf->fd = open(filename, O_RDONLY);
	if (tf->fd < 0) {
		free(tf);
		return NULL;
	}

	fstat(tf->fd, &tf->st);
	tf->map = xmmap(tf->fd, 0, tf->st.st_size);
	if (!tf->map) {
		close(tf->fd);
		free(tf);
		return NULL;
	}
	return tf;
}

static int tag_file_changed(struct tag_file *tf)
{
	struct stat st;
	fstat(tf->fd, &st);
	return st.st_mtime != tf->st.st_mtime;
}

static void free_tag_file(struct tag_file *tf)
{
	xmunmap(tf->map, tf->st.st_size);
	close(tf->fd);
}

static int parse_tag_address(struct tag_address *ta, char *buf)
{
	char ch = *buf;
	int i;

	if (ch == '/' || ch == '?') {
		// the search pattern is not real regular expression
		// need to escape special characters
		char *pattern = xnew(char, strlen(buf) * 2);
		int j = 0;

		for (i = 1; buf[i]; i++) {
			if (buf[i] == '\\' && buf[i + 1]) {
				i++;
				if (buf[i] == '\\')
					pattern[j++] = '\\';
				pattern[j++] = buf[i];
				continue;
			}
			if (buf[i] == ch) {
				pattern[j] = 0;
				ta->pattern = pattern;
				return 1;
			}
			if (buf[i] == '*')
				pattern[j++] = '\\';
			pattern[j++] = buf[i];
		}
		free(pattern);
		return 0;
	}
	ta->line = atoi(buf);
	return ta->line > 0;
}

static int do_search(struct tag_file *tf, struct tag_address *ta, const char *name)
{
	const char *ptr, *buf = tf->map;
	size_t size = tf->st.st_size;
	int name_len = strlen(name);

	memset(ta, 0, sizeof(*ta));
	ptr = buf;
	while (ptr < buf + size) {
		size_t n = buf + size - ptr;
		char *end = memchr(ptr, '\n', n);

		if (end)
			n = end - ptr;

		// tag\tfilename\t[^;]+(;.*)?
		if (name_len + 4 < n && !memcmp(ptr, name, name_len) && ptr[name_len] == '\t') {
			char *filename = xstrndup(ptr + name_len + 1, n - name_len - 1);
			char *tab = strchr(filename, '\t');

			if (tab && parse_tag_address(ta, tab + 1)) {
				*tab = 0;
				ta->filename = filename;
				return 1;
			}
			free(filename);
		}

		ptr += n + 1;
	}
	return 0;
}

void push_location(void)
{
	struct file_location *loc;

	if (!buffer->filename)
		return;
	loc = xnew(struct file_location, 1);
	loc->filename = xstrdup(buffer->filename);
	loc->line = view->cy + 1;
	list_add_after(&loc->node, &location_head);
}

void pop_location(void)
{
	struct file_location *loc;
	struct view *v;

	if (list_empty(&location_head))
		return;
	loc = container_of(location_head.next, struct file_location, node);
	list_del(&loc->node);
	v = open_buffer(loc->filename);
	if (v) {
		set_view(v);
		move_to_line(loc->line);
	}
	free(loc->filename);
	free(loc);
}

void goto_tag(const char *name)
{
	struct tag_address ta;
	struct view *v;

	if (tag_file && tag_file_changed(tag_file)) {
		free_tag_file(tag_file);
		tag_file = NULL;
	}
	if (!tag_file)
		tag_file = open_tag_file("tags");
	if (!tag_file) {
		error_msg("No tag file.");
		return;
	}
	if (!do_search(tag_file, &ta, name)) {
		error_msg("Tag %s not found.", name);
		return;
	}

	push_location();
	v = open_buffer(ta.filename);
	if (!v) {
		free(ta.filename);
		free(ta.pattern);
		return;
	}
	set_view(v);
	if (ta.pattern) {
		move_bof();
		search_tag(ta.pattern);
	} else {
		move_to_line(ta.line);
	}
	free(ta.filename);
	free(ta.pattern);
}
