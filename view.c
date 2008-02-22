#include "buffer.h"

struct view *view_new(struct window *w, struct buffer *b)
{
	struct view *v = xnew0(struct view, 1);

	v->buffer = b;
	v->window = w;

	v->cursor.head = &b->blocks;
	v->cursor.blk = BLOCK(b->blocks.next);
	return v;
}

void view_delete(struct view *v)
{
	list_del(&v->node);
	free(v);
}
