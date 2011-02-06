#include "history.h"
#include "common.h"
#include "editor.h"
#include "wbuf.h"

struct history_entry {
	struct list_head node;
	char text[1];
};

struct history search_history = {
	.head = LIST_HEAD_INIT(search_history.head),
	.nr_entries = 0,
	.max_entries = 100,
};

struct history command_history = {
	.head = LIST_HEAD_INIT(command_history.head),
	.nr_entries = 0,
	.max_entries = 500,
};

static struct list_head *search_pos;
static char *search_text;

void history_add(struct history *history, const char *text)
{
	struct history_entry *item;
	int len = strlen(text);

	if (!len)
		return;

	// don't add identical entries
	list_for_each_entry(item, &history->head, node) {
		if (!strcmp(item->text, text)) {
			// move identical entry to first
			list_del(&item->node);
			list_add_after(&item->node, &history->head);
			return;
		}
	}

	if (history->nr_entries == history->max_entries) {
		struct list_head *node = history->head.prev;
		list_del(node);
		item = container_of(node, struct history_entry, node);
		free(item);
		history->nr_entries--;
	}

	item = xmalloc(sizeof(struct history_entry) + len);
	memcpy(item->text, text, len + 1);
	list_add_after(&item->node, &history->head);
	history->nr_entries++;
}

void history_reset_search(void)
{
	free(search_text);
	search_text = NULL;
	search_pos = NULL;
}

const char *history_search_forward(struct history *history, const char *text)
{
	struct list_head *item;
	int search_len;

	if (search_pos) {
		item = search_pos->next;
	} else {
		// NOTE: not freed in history_search_backward()
		free(search_text);

		item = history->head.next;
		search_text = xstrdup(text);
	}
	search_len = strlen(search_text);
	while (item != &history->head) {
		struct history_entry *hentry;

		hentry = container_of(item, struct history_entry, node);
		if (!strncmp(search_text, hentry->text, search_len)) {
			search_pos = item;
			return hentry->text;
		}
		item = item->next;
	}
	return NULL;
}

const char *history_search_backward(struct history *history)
{
	struct list_head *item;
	int search_len;

	if (!search_pos)
		return NULL;
	item = search_pos->prev;
	search_len = strlen(search_text);
	while (item != &history->head) {
		struct history_entry *hentry;

		hentry = container_of(item, struct history_entry, node);
		if (!strncmp(search_text, hentry->text, search_len)) {
			search_pos = item;
			return hentry->text;
		}
		item = item->prev;
	}
	// NOTE: search_text is freed in history_search_forward()
	search_pos = NULL;
	return search_text;
}

void history_load(struct history *history, const char *filename)
{
	char *buf;
	ssize_t size, pos = 0;

	size = read_file(filename, &buf);
	if (size < 0) {
		if (errno != ENOENT)
			error_msg("Error reading %s: %s", filename, strerror(errno));
		return;
	}
	while (pos < size)
		history_add(history, buf_next_line(buf, &pos, size));
	free(buf);
}

void history_save(struct history *history, const char *filename)
{
	struct history_entry *item;
	WBUF(buf);

	buf.fd = open(filename, O_CREAT | O_WRONLY | O_TRUNC, 0666);
	if (buf.fd < 0)
		return;
	list_for_each_entry_reverse(item, &history->head, node) {
		wbuf_write_str(&buf, item->text);
		wbuf_write_ch(&buf, '\n');
	}
	wbuf_flush(&buf);
	close(buf.fd);
}
