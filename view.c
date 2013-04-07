#include "buffer.h"
#include "uchar.h"

struct view *view;

void view_update_cursor_y(struct view *v)
{
	struct buffer *b = v->buffer;
	struct block *blk;
	unsigned int nl = 0;

	list_for_each_entry(blk, &b->blocks, node) {
		if (blk == v->cursor.blk) {
			nl += count_nl(blk->data, v->cursor.offset);
			v->cy = nl;
			return;
		}
		nl += blk->nl;
	}
	BUG_ON(1);
}

void view_update_cursor_x(struct view *v)
{
	unsigned int tw = v->buffer->options.tab_width;
	long idx = 0;
	struct lineref lr;
	int c = 0;
	int w = 0;

	v->cx = fetch_this_line(&v->cursor, &lr);
	while (idx < v->cx) {
		unsigned int u = lr.line[idx++];

		c++;
		if (likely(u < 0x80)) {
			if (!u_is_ctrl(u)) {
				w++;
			} else if (u == '\t') {
				w = (w + tw) / tw * tw;
			} else {
				w += 2;
			}
		} else {
			idx--;
			u = u_get_nonascii(lr.line, lr.size, &idx);
			w += u_char_width(u);
		}
	}
	v->cx_char = c;
	v->cx_display = w;
}

int view_get_preferred_x(struct view *v)
{
	if (v->preferred_x < 0) {
		view_update_cursor_x(v);
		v->preferred_x = v->cx_display;
	}
	return v->preferred_x;
}

bool view_can_close(struct view *v)
{
	if (!buffer_modified(v->buffer)) {
		return true;
	}
	// open in another window?
	return v->buffer->views.count > 1;
}
