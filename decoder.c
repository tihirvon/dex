#include "decoder.h"
#include "editor.h"
#include "uchar.h"
#include "xmalloc.h"
#include "cconv.h"

static int fill(struct file_decoder *dec)
{
	size_t icount = dec->isize - dec->ipos;
	size_t max = 7 * 1024; // smaller than cconv.obuf to make realloc less likely

	if (icount > max)
		icount = max;

	if (dec->ipos == dec->isize)
		return 0;

	cconv_process(dec->cconv, dec->ibuf + dec->ipos, icount);
	dec->ipos += icount;
	if (dec->ipos == dec->isize) {
		// must be flushed after all input has been fed
		cconv_flush(dec->cconv);
	}
	return 1;
}

static int set_encoding(struct file_decoder *dec, const char *encoding);

static int detect(struct file_decoder *dec, const unsigned char *line, ssize_t len)
{
	ssize_t i = 0;

	for (; i < len; i++) {
		if (line[i] >= 0x80) {
			unsigned int idx = i;
			unsigned int u = u_get_nonascii(line, len, &idx);
			const char *encoding;

			if (u_is_unicode(u)) {
				encoding = "UTF-8";
			} else if (!strcmp(charset, "UTF-8")) {
				// UTF-8 terminal, assuming latin1
				encoding = "ISO-8859-1";
			} else {
				// assuming locale's encoding
				encoding = charset;
			}
			if (set_encoding(dec, encoding)) {
				// FIXME: error message?
				set_encoding(dec, "UTF-8");
			}
			return 1;
		}
	}

	// ASCII
	return 0;
}

static int decode_and_read_line(struct file_decoder *dec, char **linep, ssize_t *lenp)
{
	char *line;
	ssize_t len;

	while (1) {
		line = cconv_consume_line(dec->cconv, &len);
		if (line)
			break;

		if (!fill(dec))
			break;
	}

	if (line) {
		// newline not wanted
		len--;
	} else {
		line = cconv_consume_all(dec->cconv, &len);
		if (len == 0)
			return 0;
	}

	*linep = line;
	*lenp = len;
	return 1;
}

static int read_utf8_line(struct file_decoder *dec, char **linep, ssize_t *lenp)
{
	char *line = (char *)dec->ibuf + dec->ipos;
	const char *nl = memchr(line, '\n', dec->isize - dec->ipos);
	ssize_t len;

	if (nl) {
		len = nl - line;
		dec->ipos += len + 1;
	} else {
		len = dec->isize - dec->ipos;
		if (len == 0)
			return 0;
		dec->ipos += len;
	}

	*linep = line;
	*lenp = len;
	return 1;
}

static int detect_and_read_line(struct file_decoder *dec, char **linep, ssize_t *lenp)
{
	char *line = (char *)dec->ibuf + dec->ipos;
	const char *nl = memchr(line, '\n', dec->isize - dec->ipos);
	ssize_t len;

	if (nl) {
		len = nl - line;
	} else {
		len = dec->isize - dec->ipos;
		if (len == 0)
			return 0;
	}

	if (detect(dec, line, len)) {
		// encoding detected
		return dec->read_line(dec, linep, lenp);
	}

	// only ASCII so far
	dec->ipos += len;
	if (nl)
		dec->ipos++;
	*linep = line;
	*lenp = len;
	return 1;
}

static int set_encoding(struct file_decoder *dec, const char *encoding)
{
	if (!strcmp(encoding, "UTF-8")) {
		dec->read_line = read_utf8_line;
	} else {
		dec->cconv = cconv_to_utf8(encoding);
		if (dec->cconv == NULL) {
			return -1;
		}
		dec->read_line = decode_and_read_line;
	}
	dec->encoding = xstrdup(encoding);
	return 0;
}

struct file_decoder *new_file_decoder(const char *encoding, const unsigned char *buf, ssize_t size)
{
	struct file_decoder *dec = xnew0(struct file_decoder, 1);

	dec->ibuf = buf;
	dec->isize = size;
	dec->read_line = detect_and_read_line;

	if (encoding) {
		if (set_encoding(dec, encoding)) {
			free_file_decoder(dec);
			return NULL;
		}
	}
	return dec;
}

void free_file_decoder(struct file_decoder *dec)
{
	if (dec->cconv != NULL)
		cconv_free(dec->cconv);
	free(dec->encoding);
	free(dec);
}

int file_decoder_read_line(struct file_decoder *dec, char **linep, ssize_t *lenp)
{
	return dec->read_line(dec, linep, lenp);
}
