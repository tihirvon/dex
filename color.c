#include "color.h"
#include "ptr-array.h"
#include "common.h"
#include "editor.h"

static const char * const color_names[] = {
	"keep", "default",
	"black", "red", "green", "yellow", "blue", "magenta", "cyan", "gray",
	"darkgray", "lightred", "lightgreen", "lightyellow", "lightblue",
	"lightmagenta", "lightcyan", "white",
};

static const char * const attr_names[] = {
	"bold", "lowintensity", "underline", "blink", "reverse", "invisible", "keep"
};

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

static int parse_color(const char *str, int *val)
{
	char *end;
	long lval;
	int i;

	lval = strtol(str, &end, 10);
	if (*str && !*end) {
		if (lval < -2 || lval > 255)
			return 0;
		*val = lval;
		return 1;
	}
	for (i = 0; i < ARRAY_COUNT(color_names); i++) {
		if (!strcasecmp(str, color_names[i])) {
			*val = i - 2;
			return 1;
		}
	}
	return 0;
}

static int parse_attr(const char *str, unsigned short *attr)
{
	int i;

	for (i = 0; i < ARRAY_COUNT(attr_names); i++) {
		if (!strcasecmp(str, attr_names[i])) {
			*attr |= 1 << i;
			return 1;
		}
	}
	return 0;
}

int parse_term_color(struct term_color *color, char **strs)
{
	int i, count = 0;

	color->fg = -1;
	color->bg = -1;
	color->attr = 0;
	for (i = 0; strs[i]; i++) {
		const char *str = strs[i];
		int val;

		if (parse_color(str, &val)) {
			if (count > 1) {
				if (val == -2) {
					// "keep" is also a valid attribute
					color->attr |= ATTR_KEEP;
				} else {
					error_msg("too many colors");
					return 0;
				}
			} else {
				if (!count)
					color->fg = val;
				else
					color->bg = val;
				count++;
			}
		} else if (!parse_attr(str, &color->attr)) {
			error_msg("invalid color or attribute %s", str);
			return 0;
		}
	}
	return 1;
}
