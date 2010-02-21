#ifndef BUFFER_HIGHLIGHT_H
#define BUFFER_HIGHLIGHT_H

#include "buffer.h"

void update_hl_insert(unsigned int lines, int count);
void highlight_buffer(struct buffer *b);

#endif
