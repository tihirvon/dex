#include "file-history.h"
#include "common.h"
#include "editor.h"
#include "list.h"
#include "util.h"
#include "wbuf.h"

struct history_entry {
	struct list_head node;
	int x, y;
	char *filename;
};

static LIST_HEAD(history_head);
static int history_size;
static int max_history_size = 50;

static void add_entry(struct history_entry *e)
{
	list_add_after(&e->node, &history_head);
}

void add_file_history(int x, int y, const char *filename)
{
	struct history_entry *e;

	list_for_each_entry(e, &history_head, node) {
		if (!strcmp(filename, e->filename)) {
			e->x = x;
			e->y = y;
			list_del(&e->node);
			add_entry(e);
			return;
		}
	}

	while (history_size >= max_history_size) {
		e = container_of(history_head.prev, struct history_entry, node);
		list_del(&e->node);
		free(e->filename);
		free(e);
		history_size--;
	}

	if (!max_history_size)
		return;

	e = xnew(struct history_entry, 1);
	e->x = x;
	e->y = y;
	e->filename = xstrdup(filename);
	add_entry(e);
	history_size++;
}

void load_file_history(void)
{
	const char *filename = editor_file("file-history");
	int fd = open(filename, O_RDONLY);
	struct stat st;

	if (fd < 0)
		return;
	fstat(fd, &st);
	if (st.st_size) {
		char *buf = xnew(char, st.st_size + 1);
		int rc = xread(fd, buf, st.st_size);
		int pos = 0;
		if (rc < 0) {
			free(buf);
			close(fd);
			return;
		}
		while (pos < st.st_size) {
			int avail = st.st_size - pos;
			char *line = buf + pos;
			char *nl = memchr(line, '\n', avail);
			char *end;
			long x, y;

			if (nl) {
				*nl = 0;
				pos += nl - line + 1;
			} else {
				line[avail] = 0;
				pos += avail;
			}

			y = strtol(line, &end, 10);
			line = end;
			while (isspace(*line))
				line++;
			x = strtol(line, &end, 10);
			line = end;
			while (isspace(*line))
				line++;
			if (x > 0 && y > 0)
				add_file_history(x - 1, y - 1, line);
		}
		free(buf);
	}
	close(fd);
}

void save_file_history(void)
{
	const char *filename = editor_file("file-history");
	struct history_entry *e;
	WBUF(buf);

	buf.fd = open(filename, O_CREAT | O_WRONLY | O_TRUNC, 0666);
	if (buf.fd < 0)
		return;
	list_for_each_entry_reverse(e, &history_head, node) {
		char str[64];
		snprintf(str, sizeof(str), "%d %d ", e->y + 1, e->x + 1);
		wbuf_write_str(&buf, str);
		wbuf_write_str(&buf, e->filename);
		wbuf_write_ch(&buf, '\n');
	}
	wbuf_flush(&buf);
	close(buf.fd);
}

int find_file_in_history(const char *filename, int *x, int *y)
{
	struct history_entry *e;

	list_for_each_entry(e, &history_head, node) {
		if (!strcmp(filename, e->filename)) {
			*x = e->x;
			*y = e->y;
			return 1;
		}
	}
	return 0;
}
