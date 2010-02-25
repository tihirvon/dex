#ifndef BUFFER_HIGHLIGHT_H
#define BUFFER_HIGHLIGHT_H

#include "buffer.h"

extern const char *hl_buffer;
extern size_t hl_buffer_len;

void fetch_line(struct block_iter *bi);
void update_hl_insert(unsigned int lines, int count);
void highlight_buffer(struct buffer *b);

#endif
