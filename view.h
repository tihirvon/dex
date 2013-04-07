#ifndef VIEW_H
#define VIEW_H

#include "libc.h"

struct view;

void view_update_cursor_y(struct view *v);
void view_update_cursor_x(struct view *v);
int view_get_preferred_x(struct view *v);
bool view_can_close(struct view *v);

#endif
