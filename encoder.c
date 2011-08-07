#include "encoder.h"
#include "uchar.h"

struct file_encoder *new_file_encoder(const char *encoding, enum newline_sequence nls, int fd)
{
	struct file_encoder *enc = xnew0(struct file_encoder, 1);

	enc->cd = (iconv_t)-1;
	enc->nls = nls;
	enc->fd = fd;

	if (strcmp(encoding, "UTF-8")) {
		char buf[128];

		// Enable transliteration if supported.
		snprintf(buf, sizeof(buf), "%s//TRANSLIT", encoding);
		enc->cd = iconv_open(buf, "UTF-8");
		if (enc->cd == (iconv_t)-1)
			enc->cd = iconv_open(encoding, "UTF-8");

		if (enc->cd == (iconv_t)-1) {
			free(enc);
			return NULL;
		}
	}
	return enc;
}

void free_file_encoder(struct file_encoder *enc)
{
	if (enc->cd != (iconv_t)-1)
		iconv_close(enc->cd);
	free(enc->nbuf);
	free(enc->ebuf);
	free(enc);
}

static ssize_t unix_to_dos(struct file_encoder *enc, const unsigned char *buf, ssize_t size)
{
	ssize_t s, d;

	if (enc->nsize < size * 2) {
		enc->nsize = size * 2;
		xrenew(enc->nbuf, enc->nsize);
	}

	for (s = 0, d = 0; s < size; s++) {
		unsigned char ch = buf[s];
		if (ch == '\n')
			enc->nbuf[d++] = '\r';
		enc->nbuf[d++] = ch;
	}
	return d;
}

static ssize_t encode(struct file_encoder *enc, const unsigned char *buf, ssize_t size)
{
	ssize_t ipos = 0;
	ssize_t opos = 0;

	if (enc->esize < size * 4) {
		enc->esize = size * 4;
		xrenew(enc->ebuf, enc->esize);
	}

	while (ipos < size) {
		size_t ic, oc, icsave, ocsave, rc;
		char *ib, *ob;

		ib = (char *)buf + ipos;
		ic = size - ipos;
		ob = enc->ebuf + opos;
		oc = enc->esize - opos;

		icsave = ic;
		ocsave = oc;

		rc = iconv(enc->cd, (void *)&ib, &ic, &ob, &oc);
		if (rc == (size_t)-1) {
			unsigned int idx = 0;

			switch (errno) {
			case EILSEQ:
			case EINVAL:
				// can't convert this character
				u_buf_get_char(ib, ic, &idx);
				ic -= idx;
				ob[0] = '?';
				oc--;

				// reset
				iconv(enc->cd, NULL, 0, NULL, 0);

				enc->errors++;
				break;
			case E2BIG:
			default:
				return -1;
			}
		} else if (rc) {
			enc->errors += rc;
		}
		ipos += icsave - ic;
		opos += ocsave - oc;
	}
	return opos;
}

// NOTE: buf must contain whole characters!
ssize_t file_encoder_write(struct file_encoder *enc, const unsigned char *buf, ssize_t size)
{
	if (enc->nls == NEWLINE_DOS) {
		size = unix_to_dos(enc, buf, size);
		buf = enc->nbuf;
	}

	if (enc->cd == (iconv_t)-1)
		return xwrite(enc->fd, buf, size);

	size = encode(enc, buf, size);
	if (size < 0)
		return size;
	return xwrite(enc->fd, enc->ebuf, size);
}
