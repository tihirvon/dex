#include "history.h"
#include "common.h"
#include "util.h"
#include "wbuf.h"

struct history_entry {
	struct list_head node;
	char text[0];
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
	int i, len = strlen(text);

	for (i = 0; i < len; i++) {
		if (text[i] != ' ')
			break;
	}
	if (i == len)
		return;

	// don't add identical entries
	list_for_each_entry(item, &history->head, node) {
		if (!strcmp(item->text, text)) {
			// move indentical entry to first
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

	item = xmalloc(sizeof(struct history_entry) + len + 1);
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
			if (nl) {
				*nl = 0;
				pos += nl - line + 1;
			} else {
				line[avail] = 0;
				pos += avail;
			}
			history_add(history, line);
		}
		free(buf);
	}
	close(fd);
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
