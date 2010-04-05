#include "uchar.h"
#include "common.h"

#include <wctype.h>

const char hex_tab[16] = "0123456789abcdef";

int u_char_width(uchar u)
{
	if (unlikely(u < 0x20 || u == 0x7f))
		return 2;

	if (likely(u < 0x1100U))
		return 1;

	/* Hangul Jamo init. consonants */
	if (u <= 0x115fU)
		goto wide;

	/* angle brackets */
	if (u == 0x2329U || u == 0x232aU)
		goto wide;

	if (u < 0x2e80U)
		goto narrow;
	/* CJK ... Yi */
	if (u < 0x302aU)
		goto wide;
	if (u <= 0x302fU)
		goto narrow;
	if (u == 0x303fU)
		goto narrow;
	if (u == 0x3099U)
		goto narrow;
	if (u == 0x309aU)
		goto narrow;
	/* CJK ... Yi */
	if (u <= 0xa4cfU)
		goto wide;

	/* Hangul Syllables */
	if (u >= 0xac00U && u <= 0xd7a3U)
		goto wide;

	/* CJK Compatibility Ideographs */
	if (u >= 0xf900U && u <= 0xfaffU)
		goto wide;

	/* CJK Compatibility Forms */
	if (u >= 0xfe30U && u <= 0xfe6fU)
		goto wide;

	/* Fullwidth Forms */
	if (u >= 0xff00U && u <= 0xff60U)
		goto wide;

	/* Fullwidth Forms */
	if (u >= 0xffe0U && u <= 0xffe6U)
		goto wide;

	/* CJK extra stuff */
	if (u >= 0x20000U && u <= 0x2fffdU)
		goto wide;

	/* ? */
	if (u >= 0x30000U && u <= 0x3fffdU)
		goto wide;

	/* invalid bytes in unicode stream are rendered "<xx>" */
	if (u & U_INVALID_MASK)
		return 4;
narrow:
	return 1;
wide:
	return 2;
}

unsigned int u_str_width(const char *str, unsigned int size)
{
	unsigned int idx = 0, w = 0;

	while (idx < size)
		w += u_char_width(u_buf_get_char(str, size, &idx));
	return w;
}

uchar u_prev_char(const char *str, unsigned int *idx)
{
	const unsigned char *s = (const unsigned char *)str;
	unsigned int i = *idx;
	unsigned int count, shift;
	uchar u;

	u = s[--i];
	if (likely(u < 0x80)) {
		*idx = i;
		return u;
	}

	if (!u_is_continuation(u))
		goto invalid;

	u &= 0x3f;
	count = 1;
	shift = 6;
	while (i) {
		uchar ch = s[--i];
		unsigned int len = u_seq_len(ch);

		count++;
		if (len == 0) {
			if (count == 4) {
				/* too long sequence */
				break;
			}
			u |= (ch & 0x3f) << shift;
			shift += 6;
		} else if (count != len) {
			/* incorrect length */
			break;
		} else {
			u |= (ch & u_get_first_byte_mask(len)) << shift;
			if (!u_seq_len_ok(u, len))
				break;

			*idx = i;
			return u;
		}
	}
invalid:
	*idx = *idx - 1;
	return u | U_INVALID_MASK;
}

uchar u_buf_get_char(const char *buf, unsigned int size, unsigned int *idx)
{
	const unsigned char *s = (const unsigned char *)buf;
	unsigned int i = *idx;
	int len, c;
	uchar first, u;

	first = s[i++];
	if (likely(first < 0x80)) {
		*idx = i;
		return first;
	}

	len = u_seq_len(first);
	if (unlikely(len < 2 || len > size - i + 1))
		goto invalid;

	u = first & u_get_first_byte_mask(len);
	c = len - 1;
	do {
		uchar ch = s[i++];
		if (!u_is_continuation(ch))
			goto invalid;
		u = (u << 6) | (ch & 0x3f);
	} while (--c);

	if (!u_seq_len_ok(u, len))
		goto invalid;

	*idx = i;
	return u;
invalid:
	*idx += 1;
	return first | U_INVALID_MASK;
}

void u_set_char_raw(char *str, unsigned int *idx, uchar uch)
{
	unsigned int i = *idx;

	if (uch <= 0x7fU) {
		str[i++] = uch;
		*idx = i;
	} else if (uch <= 0x7ffU) {
		str[i + 1] = (uch & 0x3f) | 0x80; uch >>= 6;
		str[i + 0] = uch | 0xc0U;
		i += 2;
		*idx = i;
	} else if (uch <= 0xffffU) {
		str[i + 2] = (uch & 0x3f) | 0x80; uch >>= 6;
		str[i + 1] = (uch & 0x3f) | 0x80; uch >>= 6;
		str[i + 0] = uch | 0xe0U;
		i += 3;
		*idx = i;
	} else if (uch <= 0x10ffffU) {
		str[i + 3] = (uch & 0x3f) | 0x80; uch >>= 6;
		str[i + 2] = (uch & 0x3f) | 0x80; uch >>= 6;
		str[i + 1] = (uch & 0x3f) | 0x80; uch >>= 6;
		str[i + 0] = uch | 0xf0U;
		i += 4;
		*idx = i;
	} else {
		/* must be an invalid uchar */
		str[i++] = uch & 0xff;
		*idx = i;
	}
}

void u_set_char(char *str, unsigned int *idx, uchar uch)
{
	unsigned int i = *idx;

	if (unlikely(uch < 0x20))
		goto control;

	if (unlikely(uch == 0x7f))
		goto delete;

	if (uch <= 0x7fU) {
		str[i++] = uch;
		*idx = i;
		return;
	} else if (uch <= 0x7ffU) {
		str[i + 1] = (uch & 0x3f) | 0x80; uch >>= 6;
		str[i + 0] = uch | 0xc0U;
		i += 2;
		*idx = i;
		return;
	} else if (uch <= 0xffffU) {
		str[i + 2] = (uch & 0x3f) | 0x80; uch >>= 6;
		str[i + 1] = (uch & 0x3f) | 0x80; uch >>= 6;
		str[i + 0] = uch | 0xe0U;
		i += 3;
		*idx = i;
		return;
	} else if (uch <= 0x10ffffU) {
		str[i + 3] = (uch & 0x3f) | 0x80; uch >>= 6;
		str[i + 2] = (uch & 0x3f) | 0x80; uch >>= 6;
		str[i + 1] = (uch & 0x3f) | 0x80; uch >>= 6;
		str[i + 0] = uch | 0xf0U;
		i += 4;
		*idx = i;
		return;
	}

	/* invalid */
	str[i++] = '<';
	str[i++] = hex_tab[(uch >> 4) & 0x0f];
	str[i++] = hex_tab[uch & 0x0f];
	str[i++] = '>';
	*idx = i;
	return;
control:
	str[i++] = '^';
	str[i++] = uch | 0x40;
	*idx = i;
	return;
delete:
	str[i++] = '^';
	str[i++] = '?';
	*idx = i;
	return;
}

unsigned int u_skip_chars(const char *str, int *width)
{
	int w = *width;
	unsigned int idx = 0;

	while (w > 0) {
		uchar u = u_buf_get_char(str, idx + 4, &idx);
		w -= u_char_width(u);
	}
	/* add 1..3 if skipped 'too much' (the last char was double width or invalid (<xx>)) */
	*width -= w;
	return idx;
}
