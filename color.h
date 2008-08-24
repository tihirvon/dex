#ifndef COLOR_H
#define COLOR_H

#include "term.h"

struct hl_color {
	char *name;
	struct term_color color;
};

struct hl_color *set_highlight_color(const char *name, const struct term_color *color);
struct hl_color *find_color(const char *name);

#endif
