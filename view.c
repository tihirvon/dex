#include "buffer.h"

struct view *view_new(struct window *w, struct buffer *b)
{
	struct view *v = xnew0(struct view, 1);

	v->buffer = b;
	v->window = w;
	v->cblk = BLOCK(b->blocks.next);
	return v;
}

void view_delete(struct view *v)
{
	list_del(&v->node);
	free(v);
}
