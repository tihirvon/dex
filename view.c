#include "window.h"
#include "file-history.h"

struct view *view_new(struct window *w, struct buffer *b)
{
	struct view *v = xnew0(struct view, 1);

	b->ref++;

	v->buffer = b;
	v->window = w;
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
