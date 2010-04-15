#ifndef BLOCK_H
#define BLOCK_H

struct block *block_new(unsigned int size);
void do_insert(const char *buf, unsigned int len);
char *do_delete(unsigned int len);

#endif
