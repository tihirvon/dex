#ifndef ENCODER_H
#define ENCODER_H

#include "common.h"
#include "options.h"

#include <iconv.h>

struct file_encoder {
	unsigned char *ebuf;
	unsigned char *nbuf;
	ssize_t esize;
	ssize_t nsize;
	iconv_t cd;
	enum newline_sequence nls;
	int fd;
	int errors;
};

struct file_encoder *new_file_encoder(const char *encoding, enum newline_sequence nls, int fd);
void free_file_encoder(struct file_encoder *enc);
ssize_t file_encoder_write(struct file_encoder *enc, const unsigned char *buf, ssize_t size);

#endif
