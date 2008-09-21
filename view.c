#include "window.h"
#include "file-history.h"

static void restore_cursor_position(struct view *v)
{
	int x, y;

	if (!v->buffer->abs_filename)
		return;

	if (find_file_in_history(v->buffer->abs_filename, &x, &y)) {
		struct view *save = view;

		/* most commands work on current view */
		set_view(v);
		move_to_line(y + 1);
		move_to_column(x + 1);

		if (save)
			set_view(save);
	}
}

struct view *view_new(struct window *w, struct buffer *b)
{
	struct view *v = xnew0(struct view, 1);

	b->ref++;

	v->buffer = b;
	v->window = w;

	v->cursor.head = &b->blocks;
	v->cursor.blk = BLOCK(b->blocks.next);

	restore_cursor_position(v);
	return v;
}

void view_delete(struct view *v)
{
	struct buffer *b = v->buffer;

	if (!--b->ref) {
		if (b->abs_filename)
			add_file_history(v->cx_display, v->cy, b->abs_filename);
		free_buffer(b);
	}
	list_del(&v->node);
	free(v);
}
