#include "common.h"
#include "color.h"

static struct hl_color **hl_colors;
static int color_count;
static int color_alloc;

struct hl_color *set_highlight_color(const char *name, const struct term_color *color)
{
	struct hl_color *c;
	int i;

	for (i = 0; i < color_count; i++) {
		c = hl_colors[i];
		if (!strcmp(name, c->name)) {
			c->color = *color;
			return c;
		}
	}

	if (color_count == color_alloc) {
		color_alloc = color_alloc * 3 / 2;
		color_alloc = (color_alloc + 4) & ~3;
		xrenew(hl_colors, color_alloc);
	}
	c = xnew(struct hl_color, 1);
	c->name = xstrdup(name);
	c->color = *color;
	hl_colors[color_count++] = c;
	return c;
}

struct hl_color *find_color(const char *name)
{
	int i;

	for (i = 0; i < color_count; i++) {
		if (!strcmp(hl_colors[i]->name, name))
			return hl_colors[i];
	}
	return NULL;
}
