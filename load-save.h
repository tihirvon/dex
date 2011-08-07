#ifndef LOAD_SAVE_H
#define LOAD_SAVE_H

#include "buffer.h"

int load_buffer(struct buffer *b, int must_exist);
int save_buffer(const char *filename, const char *encoding, enum newline_sequence newline);

#endif
