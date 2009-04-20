#include "common.h"
#include "color.h"
#include "ptr-array.h"

static PTR_ARRAY(hl_colors);

struct hl_color *set_highlight_color(const char *name, const struct term_color *color)
{
	struct hl_color *c;
	int i;

	for (i = 0; i < hl_colors.count; i++) {
		c = hl_colors.ptrs[i];
		if (!strcmp(name, c->name)) {
			c->color = *color;
			return c;
		}
	}

	c = xnew(struct hl_color, 1);
	c->name = xstrdup(name);
	c->color = *color;
	ptr_array_add(&hl_colors, c);
	return c;
}

static struct hl_color *find_real_color(const char *name)
{
	int i;

	for (i = 0; i < hl_colors.count; i++) {
		struct hl_color *c = hl_colors.ptrs[i];
		if (!strcmp(c->name, name))
			return c;
	}
	return NULL;
}

struct hl_color *find_color(const char *name)
{
	struct hl_color *color = find_real_color(name);
	const char *dot;

	if (color)
		return color;
	dot = strchr(name, '.');
	if (dot)
		return find_real_color(dot + 1);
	return NULL;
}
