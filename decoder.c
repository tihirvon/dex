#include "decoder.h"
#include "editor.h"
#include "uchar.h"

static int fill(struct file_decoder *dec)
{
	ssize_t ofree;
	size_t ic, oc, icsave, ocsave, rc;
	char *ib, *ob;

	if (dec->ipos == dec->isize)
		return 0;

	memmove(dec->obuf, dec->obuf + dec->opos, dec->ofill - dec->opos);
	dec->ofill -= dec->opos;
	dec->opos = 0;

	ofree = dec->osize - dec->ofill;
	if (ofree < 128) {
		dec->osize *= 2;
		xrenew(dec->obuf, dec->osize);
		ofree = dec->osize - dec->ofill;
	}

	ib = (char *)dec->ibuf + dec->ipos;
	ic = dec->isize - dec->ipos;
	ob = dec->obuf + dec->ofill;
	oc = ofree;

	if (ic > oc / 4)
		ic = oc / 4;

	icsave = ic;
	ocsave = oc;

	rc = iconv(dec->cd, (void *)&ib, &ic, &ob, &oc);
	if (rc == (size_t)-1) {
		switch (errno) {
		case EILSEQ:
		case EINVAL:
			// can't convert this byte
			ob[0] = ib[0];
			ic--;
			oc--;

			// reset
			iconv(dec->cd, NULL, NULL, NULL, NULL);
			break;
		case E2BIG:
		default:
			// FIXME
			return 0;
		}
	}

	ic = icsave - ic;
	oc = ocsave - oc;

	dec->ipos += ic;
	dec->ofill += oc;
	return 1;
}

static int set_encoding(struct file_decoder *dec, const char *encoding);

static int detect(struct file_decoder *dec, const unsigned char *line, ssize_t len)
{
	const unsigned long *lline = (const unsigned long *)line;
	unsigned long mask = 0x8080808080808080UL;
	ssize_t i, llen = len / sizeof(long);

	for (i = 0; i < llen; i++) {
		if (lline[i] & mask)
			break;
	}

	i *= sizeof(long);
	for (; i < len; i++) {
		if (line[i] >= 0x80) {
			unsigned int idx = i;
			unsigned int u = u_get_nonascii(line, len, &idx);

			if (u_is_valid(u)) {
				set_encoding(dec, "UTF-8");
				return 1;
			}

			if (!strcmp(charset, "UTF-8")) {
				// UTF-8 terminal, assuming latin1
				set_encoding(dec, "ISO-8859-1");
			} else {
				// assuming locale's encoding
				set_encoding(dec, charset);
			}
			return 1;
		}
	}

	// ASCII
	return 0;
}

static int decode_and_read_line(struct file_decoder *dec, char **linep, ssize_t *lenp)
{
	const char *nl;
	char *line;
	ssize_t len;

	while (1) {
		nl = memchr(dec->obuf + dec->opos, '\n', dec->ofill - dec->opos);
		if (nl)
			break;

		if (!fill(dec))
			break;
	}

	line = dec->obuf + dec->opos;
	if (nl) {
		len = nl - line;
		dec->opos += len + 1;
	} else {
		len = dec->ofill - dec->opos;
		if (len == 0)
			return 0;
		dec->opos += len;
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
	dec->encoding = xstrdup(encoding);
	if (!strcmp(encoding, "UTF-8")) {
		dec->read_line = read_utf8_line;
		return 0;
	}

	dec->cd = iconv_open("UTF-8", encoding);
	if (dec->cd == (iconv_t)-1) {
		free(dec->encoding);
		free(dec);
		return -1;
	}

	dec->osize = 32 * 1024;
	dec->obuf = xnew(unsigned char, dec->osize);
	dec->read_line = decode_and_read_line;
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
	if (dec->encoding && strcmp(dec->encoding, "UTF-8"))
		iconv_close(dec->cd);
	free(dec->obuf);
	free(dec->encoding);
	free(dec);
}

int file_decoder_read_line(struct file_decoder *dec, char **linep, ssize_t *lenp)
{
	return dec->read_line(dec, linep, lenp);
}
