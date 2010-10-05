#ifndef HL_H
#define HL_H

#include "state.h"

struct hl_color **highlight_line(struct state *state, const char *line, int len, struct state **ret);

#endif
