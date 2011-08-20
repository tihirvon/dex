#include "uchar.h"
#include "common.h"

struct codepoint_range {
	unsigned int lo, hi;
};

// All these are indistinguishable from ASCII space on terminal.
static const struct codepoint_range evil_space[] = {
	{ 0x00a0, 0x00a0 }, // No-break space. Easy to type accidentally (AltGr+Space)
	{ 0x00ad, 0x00ad }, // Soft hyphen. Very very soft...
	{ 0x2000, 0x200a }, // Legacy spaces of varying sizes
	{ 0x2028, 0x2029 }, // Line and paragraph separators
	{ 0x202f, 0x202f }, // Narrow No-Break Space
	{ 0x205f, 0x205f }, // Mathematical space. Proven to be correct. Legacy
	{ 0x2800, 0x2800 }, // Braille Pattern Blank
};

static const struct codepoint_range zero_width[] = {
	{ 0x200b, 0x200f },
	{ 0x202a, 0x202e },
	{ 0x2060, 0x2063 },
	{ 0xfeff, 0xfeff },
};

const char hex_tab[16] = "0123456789abcdef";

static inline int in_range(unsigned int u, const struct codepoint_range *range, int count)
{
	int i;

	for (i = 0; i < count; i++) {
		if (u < range[i].lo)
			return 0;
		if (u <= range[i].hi)
			return 1;
	}
	return 0;
}

static unsigned int unprintable_bit(unsigned int u)
{
	// Unprintable garbage inherited from latin1.
	if (u >= 0x80 && u <= 0x9f)
		return U_UNPRINTABLE_BIT;

	if (in_range(u, zero_width, ARRAY_COUNT(zero_width)))
		return U_UNPRINTABLE_BIT;
	return 0;
}

int u_is_special_whitespace(unsigned int u)
{
	return in_range(u, evil_space, ARRAY_COUNT(evil_space));
}

int u_char_width(unsigned int u)
{
	if (unlikely(u_is_ctrl(u)))
		return 2;

	if (likely(u < 0x80))
		return 1;

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

	/* unprintable characters (includes invalid bytes in unicode stream) are rendered "<xx>" */
	if (u_is_unprintable(u))
		return 4;
narrow:
	return 1;
wide:
	return 2;
}

unsigned int u_str_width(const unsigned char *str, unsigned int size)
{
	unsigned int idx = 0, w = 0;

	while (idx < size)
		w += u_char_width(u_buf_get_char(str, size, &idx));
	return w;
}

unsigned int u_prev_char(const unsigned char *buf, unsigned int *idx)
{
	unsigned int i = *idx;
	unsigned int count, shift;
	unsigned int u;

	u = buf[--i];
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
		unsigned int ch = buf[--i];
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
			return u | unprintable_bit(u);
		}
	}
invalid:
	*idx = *idx - 1;
	return u | U_UNPRINTABLE_BIT | U_INVALID_BIT;
}

unsigned int u_buf_get_char(const unsigned char *buf, unsigned int size, unsigned int *idx)
{
	unsigned int i = *idx;
	unsigned int u = buf[i];

	if (likely(u < 0x80)) {
		*idx = i + 1;
		return u;
	}
	return u_get_nonascii(buf, size, idx);
}

unsigned int u_get_nonascii(const unsigned char *buf, unsigned int size, unsigned int *idx)
{
	unsigned int i = *idx;
	int len, c;
	unsigned int first, u;

	first = buf[i++];
	len = u_seq_len(first);
	if (unlikely(len < 2 || len > size - i + 1))
		goto invalid;

	u = first & u_get_first_byte_mask(len);
	c = len - 1;
	do {
		unsigned int ch = buf[i++];
		if (!u_is_continuation(ch))
			goto invalid;
		u = (u << 6) | (ch & 0x3f);
	} while (--c);

	if (!u_seq_len_ok(u, len))
		goto invalid;

	*idx = i;
	return u | unprintable_bit(u);
invalid:
	*idx += 1;
	return first | U_UNPRINTABLE_BIT | U_INVALID_BIT;
}

void u_set_char_raw(char *str, unsigned int *idx, unsigned int uch)
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
		/* invalid byte value */
		str[i++] = uch & 0xff;
		*idx = i;
	}
}

void u_set_char(char *str, unsigned int *idx, unsigned int uch)
{
	unsigned int i = *idx;

	if (uch < 0x80) {
		if (likely(!u_is_ctrl(uch))) {
			str[i++] = uch;
			*idx = i;
			return;
		}
		u_set_ctrl(str, idx, uch);
		return;
	}
	if (uch <= 0x7ff) {
		str[i + 1] = (uch & 0x3f) | 0x80; uch >>= 6;
		str[i + 0] = uch | 0xc0U;
		i += 2;
		*idx = i;
		return;
	}
	if (uch <= 0xffff) {
		str[i + 2] = (uch & 0x3f) | 0x80; uch >>= 6;
		str[i + 1] = (uch & 0x3f) | 0x80; uch >>= 6;
		str[i + 0] = uch | 0xe0U;
		i += 3;
		*idx = i;
		return;
	}
	if (uch <= 0x10ffff) {
		str[i + 3] = (uch & 0x3f) | 0x80; uch >>= 6;
		str[i + 2] = (uch & 0x3f) | 0x80; uch >>= 6;
		str[i + 1] = (uch & 0x3f) | 0x80; uch >>= 6;
		str[i + 0] = uch | 0xf0U;
		i += 4;
		*idx = i;
		return;
	}
	// U_UNPRINTABLE_BIT must be set
	u_set_hex(str, idx, uch);
}

void u_set_hex(char *str, unsigned int *idx, unsigned int uch)
{
	unsigned int i = *idx;

	uch &= ~(U_UNPRINTABLE_BIT | U_INVALID_BIT);

	str[i++] = '<';
	if (uch <= 0xff) {
		str[i++] = hex_tab[(uch >> 4) & 0x0f];
		str[i++] = hex_tab[uch & 0x0f];
	} else {
		str[i++] = '?';
		str[i++] = '?';
	}
	str[i++] = '>';
	*idx = i;
}

unsigned int u_skip_chars(const char *str, int *width)
{
	int w = *width;
	unsigned int idx = 0;

	while (w > 0) {
		unsigned int u = u_buf_get_char(str, idx + 4, &idx);
		w -= u_char_width(u);
	}
	/* add 1..3 if skipped 'too much' (the last char was double width or invalid (<xx>)) */
	*width -= w;
	return idx;
}
