#include "cconv.h"
#include "common.h"

#include <iconv.h>

struct cconv {
	iconv_t cd;

	char *obuf;
	size_t osize;
	size_t opos;

	size_t consumed;
	int errors;

	// temporary input buffer
	char tbuf[16];
	size_t tcount;
};

static struct cconv *create(iconv_t cd)
{
	struct cconv *c = xnew0(struct cconv, 1);
	c->cd = cd;
	c->osize = 8192;
	c->obuf = xnew(char, c->osize);
	return c;
}

static void resize_obuf(struct cconv *c)
{
	c->osize *= 2;
	xrenew(c->obuf, c->osize);
}

static void handle_invalid(struct cconv *c, const char *buf, size_t count)
{
	memcpy(c->obuf + c->opos, buf, count);
	c->opos += count;
}

static int xiconv(struct cconv *c, char **ib, size_t *ic)
{
	while (1) {
		char *ob = c->obuf + c->opos;
		size_t oc = c->osize - c->opos;
		size_t rc = iconv(c->cd, ib, ic, &ob, &oc);

		c->opos = ob - c->obuf;
		if (rc == (size_t)-1) {
			switch (errno) {
			case EILSEQ:
				c->errors++;
				// reset
				iconv(c->cd, NULL, NULL, NULL, NULL);
			case EINVAL:
				return errno;
			case E2BIG:
				resize_obuf(c);
				continue;
			default:
				BUG("iconv: %s\n", strerror(errno));
			}
		} else {
			c->errors += rc;
		}
		return 0;
	}
}

static size_t convert_incomplete(struct cconv *c, const char *input, size_t len)
{
	size_t ic, ipos = 0;
	char *ib;

	while (c->tcount < sizeof(c->tbuf) && ipos < len) {
		int rc;

		c->tbuf[c->tcount++] = input[ipos++];

		ib = c->tbuf;
		ic = c->tcount;
		rc = xiconv(c, &ib, &ic);

		if (ic > 0)
			memmove(c->tbuf, ib, ic);
		c->tcount = ic;

		switch (rc) {
		case EINVAL:
			// Incomplete character at end of input buffer.
			// try again with more input data
			continue;
		case EILSEQ:
			// Invalid multibyte sequence.
			handle_invalid(c, c->tbuf, c->tcount);
			c->tcount = 0;
			return ipos;
		}
		break;
	}
	return ipos;
}

void cconv_process(struct cconv *c, const char *input, size_t len)
{
	size_t ic;
	char *ib;

	if (c->consumed > 0) {
		size_t fill = c->opos - c->consumed;
		memmove(c->obuf, c->obuf + c->consumed, fill);
		c->opos = fill;
		c->consumed = 0;
	}

	if (c->tcount > 0) {
		size_t ipos = convert_incomplete(c, input, len);
		input += ipos;
		len -= ipos;
	}

	ib = (char *)input;
	ic = len;
	while (ic > 0) {
		switch (xiconv(c, &ib, &ic)) {
		case EINVAL:
			// Incomplete character at end of input buffer.
			if (ic < sizeof(c->tbuf)) {
				memcpy(c->tbuf, ib, ic);
				c->tcount = ic;
			} else {
				// FIXME
			}
			ic = 0;
			break;
		case EILSEQ:
			// Invalid multibyte sequence.
			handle_invalid(c, ib, 1);
			ic--;
			ib++;
			break;
		}
	}
}

struct cconv *cconv_to_utf8(const char *encoding)
{
	iconv_t cd;

	cd = iconv_open("UTF-8", encoding);
	if (cd == (iconv_t)-1)
		return NULL;
	return create(cd);
}

struct cconv *cconv_from_utf8(const char *encoding)
{
	iconv_t cd;
	char buf[128];

	// Enable transliteration if supported.
	snprintf(buf, sizeof(buf), "%s//TRANSLIT", encoding);
	cd = iconv_open(buf, "UTF-8");
	if (cd == (iconv_t)-1)
		cd = iconv_open(encoding, "UTF-8");
	if (cd == (iconv_t)-1)
		return NULL;
	return create(cd);
}

void cconv_flush(struct cconv *c)
{
	if (c->tcount > 0) {
		handle_invalid(c, c->tbuf, c->tcount);
		c->tcount = 0;
	}
}

int cconv_nr_errors(struct cconv *c)
{
	return c->errors;
}

char *cconv_consume_line(struct cconv *c, size_t *len)
{
	char *line = c->obuf + c->consumed;
	char *nl = memchr(line, '\n', c->opos - c->consumed);
	size_t n;

	if (nl == NULL) {
		*len = 0;
		return NULL;
	}
	n = nl - line + 1;
	c->consumed += n;
	*len = n;
	return line;
}

char *cconv_consume_all(struct cconv *c, size_t *len)
{
	char *buf = c->obuf + c->consumed;

	*len = c->opos - c->consumed;
	c->consumed = c->opos;
	return buf;
}

void cconv_free(struct cconv *c)
{
	iconv_close(c->cd);
	free(c->obuf);
	free(c);
}
