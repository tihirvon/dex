#ifndef UCHAR_H
#define UCHAR_H

extern const char hex_tab[16];

#define U_INVALID_MASK 0x10000000U

static inline int u_is_unicode(unsigned int uch)
{
	return uch <= 0x10ffffU;
}

static inline unsigned int u_char_size(unsigned int uch)
{
	if (uch <= 0x7fU)
		return 1;
	if (uch <= 0x7ffU)
		return 2;
	if (uch <= 0xffffU)
		return 3;
	if (uch <= 0x10ffffU)
		return 4;
	return 1;
}

static inline int u_seq_len(unsigned int first_byte)
{
	if (first_byte < 0x80)
		return 1;
	if (first_byte < 0xc0)
		return 0;
	if (first_byte < 0xe0)
		return 2;
	if (first_byte < 0xf0)
		return 3;

	// could be 0xf8 but RFC 3629 doesn't allow codepoints above 0x10ffff
	if (first_byte < 0xf5)
		return 4;
	return -1;
}

static inline int u_is_continuation(unsigned int uch)
{
	return (uch & 0xc0) == 0x80;
}

static inline int u_seq_len_ok(unsigned int uch, int len)
{
	return u_char_size(uch) == len;
}

/*
 * Len  Mask         Note
 * -------------------------------------------------
 * 1    0111 1111    Not supported by this function!
 * 2    0001 1111
 * 3    0000 1111
 * 4    0000 0111
 * 5    0000 0011    Forbidded by RFC 3629
 * 6    0000 0001    Forbidded by RFC 3629
 */
static inline unsigned int u_get_first_byte_mask(unsigned int len)
{
	return (1U << 7U >> len) - 1U;
}

static inline int u_is_ctrl(unsigned int u)
{
	return u < 0x20 || u == 0x7f;
}

static inline void u_set_ctrl(char *buf, unsigned int *idx, unsigned int u)
{
	unsigned int i = *idx;
	buf[i++] = '^';
	if (u == 0x7f)
		buf[i++] = '?';
	else
		buf[i++] = u | 0x40;
	*idx = i;
}

int u_char_width(unsigned int uch);

unsigned int u_str_width(const char *str, unsigned int size);

unsigned int u_prev_char(const char *str, unsigned int *idx);
unsigned int u_buf_get_char(const char *buf, unsigned int size, unsigned int *idx);
unsigned int u_get_nonascii(const char *buf, unsigned int size, unsigned int *idx);

void u_set_char_raw(char *str, unsigned int *idx, unsigned int uch);
void u_set_char(char *str, unsigned int *idx, unsigned int uch);
void u_set_hex(char *str, unsigned int *idx, unsigned int uch);

/*
 * Total width of skipped characters is stored back to @width.
 *
 * Stored @width can be 1 more than given width if the last skipped
 * character was double width or even 3 more if the last skipped
 * character was invalid (<xx>).
 *
 * Returns number of bytes skipped.
 */
unsigned int u_skip_chars(const char *str, int *width);

#endif
