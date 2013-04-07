#include "selection.h"
#include "buffer.h"
#include "view.h"

void init_selection(struct selection_info *info)
{
	struct block_iter ei;
	unsigned int u;

	info->so = view->sel_so;
	info->eo = block_iter_get_offset(&view->cursor);
	info->si = view->cursor;
	block_iter_goto_offset(&info->si, info->so);
	info->swapped = false;
	if (info->so > info->eo) {
		long o = info->so;
		info->so = info->eo;
		info->eo = o;
		info->si = view->cursor;
		info->swapped = true;
	}

	ei = info->si;
	block_iter_skip_bytes(&ei, info->eo - info->so);
	if (block_iter_is_eof(&ei)) {
		if (info->so == info->eo)
			return;
		info->eo -= buffer_prev_char(&ei, &u);
	}
	if (view->selection == SELECT_LINES) {
		info->so -= block_iter_bol(&info->si);
		info->eo += block_iter_eat_line(&ei);
	} else {
		// character under cursor belongs to the selection
		info->eo += buffer_next_char(&ei, &u);
	}
}

long prepare_selection(void)
{
	struct selection_info info;
	init_selection(&info);
	view->cursor = info.si;
	return info.eo - info.so;
}

int get_nr_selected_lines(struct selection_info *info)
{
	struct block_iter bi = info->si;
	long pos = info->so;
	unsigned int u = 0;
	int nr_lines = 1;

	while (pos < info->eo) {
		if (u == '\n')
			nr_lines++;
		pos += buffer_next_char(&bi, &u);
	}
	return nr_lines;
}

int get_nr_selected_chars(struct selection_info *info)
{
	struct block_iter bi = info->si;
	long pos = info->so;
	unsigned int u;
	int nr_chars = 0;

	while (pos < info->eo) {
		nr_chars++;
		pos += buffer_next_char(&bi, &u);
	}
	return nr_chars;
}
