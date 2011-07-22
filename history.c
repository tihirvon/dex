#include "history.h"
#include "common.h"
#include "editor.h"
#include "wbuf.h"
#include "ptr-array.h"

PTR_ARRAY(search_history);
PTR_ARRAY(command_history);

static int search_pos = -1;
static char *search_text;

// add item to end of array
void history_add(struct ptr_array *history, const char *text, int max_entries)
{
	int i;

	if (text[0] == 0)
		return;

	// don't add identical entries
	for (i = 0; i < history->count; i++) {
		if (!strcmp(history->ptrs[i], text)) {
			// move identical entry to end
			ptr_array_add(history, ptr_array_remove(history, i));
			return;
		}
	}
	if (history->count == max_entries)
		free(ptr_array_remove(history, 0));
	ptr_array_add(history, xstrdup(text));
}

void history_reset_search(void)
{
	free(search_text);
	search_text = NULL;
	search_pos = -1;
}

const char *history_search_forward(struct ptr_array *history, const char *text)
{
	int i = search_pos;

	if (i < 0) {
		// NOTE: not freed in history_search_backward()
		free(search_text);

		search_text = xstrdup(text);
		i = history->count;
	}
	while (--i >= 0) {
		if (str_has_prefix(history->ptrs[i], search_text)) {
			search_pos = i;
			return history->ptrs[i];
		}
	}
	return NULL;
}

const char *history_search_backward(struct ptr_array *history)
{
	int i = search_pos;

	if (i < 0)
		return NULL;

	while (++i < history->count) {
		if (str_has_prefix(history->ptrs[i], search_text)) {
			search_pos = i;
			return history->ptrs[i];
		}
	}
	// NOTE: search_text is freed in history_search_forward()
	search_pos = -1;
	return search_text;
}

void history_load(struct ptr_array *history, const char *filename, int max_entries)
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
		history_add(history, buf_next_line(buf, &pos, size), max_entries);
	free(buf);
}

void history_save(struct ptr_array *history, const char *filename)
{
	WBUF(buf);
	int i;

	buf.fd = open(filename, O_CREAT | O_WRONLY | O_TRUNC, 0666);
	if (buf.fd < 0)
		return;

	for (i = 0; i < history->count; i++) {
		wbuf_write_str(&buf, history->ptrs[i]);
		wbuf_write_ch(&buf, '\n');
	}
	wbuf_flush(&buf);
	close(buf.fd);
}
