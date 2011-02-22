#include "file-history.h"
#include "common.h"
#include "editor.h"
#include "list.h"
#include "wbuf.h"

struct history_entry {
	struct list_head node;
	int row, col;
	char *filename;
};

static LIST_HEAD(history_head);
static int history_size;
static int max_history_size = 50;

static void add_entry(struct history_entry *e)
{
	list_add_after(&e->node, &history_head);
}

void add_file_history(int row, int col, const char *filename)
{
	struct history_entry *e;

	list_for_each_entry(e, &history_head, node) {
		if (!strcmp(filename, e->filename)) {
			e->row = row;
			e->col = col;
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
	e->row = row;
	e->col = col;
	e->filename = xstrdup(filename);
	add_entry(e);
	history_size++;
}

void load_file_history(void)
{
	const char *filename = editor_file("file-history");
	ssize_t size, pos = 0;
	char *buf;

	size = read_file(filename, &buf);
	if (size < 0) {
		if (errno != ENOENT)
			error_msg("Error reading %s: %s", filename, strerror(errno));
		return;
	}
	while (pos < size) {
		char *line = buf_next_line(buf, &pos, size);
		char *end;
		long row, col;

		row = strtol(line, &end, 10);
		line = end;
		while (isspace(*line))
			line++;
		col = strtol(line, &end, 10);
		line = end;
		while (isspace(*line))
			line++;
		if (row > 0 && col > 0)
			add_file_history(row, col, line);
	}
	free(buf);
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
		snprintf(str, sizeof(str), "%d %d ", e->row, e->col);
		wbuf_write_str(&buf, str);
		wbuf_write_str(&buf, e->filename);
		wbuf_write_ch(&buf, '\n');
	}
	wbuf_flush(&buf);
	close(buf.fd);
}

int find_file_in_history(const char *filename, int *row, int *col)
{
	struct history_entry *e;

	list_for_each_entry(e, &history_head, node) {
		if (!strcmp(filename, e->filename)) {
			*row = e->row;
			*col = e->col;
			return 1;
		}
	}
	return 0;
}
