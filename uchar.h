#ifndef UCHAR_H
#define UCHAR_H

typedef unsigned int uchar;

extern const char hex_tab[16];
extern int u_min_val[4];
extern int u_max_val[4];

/*
 * Invalid bytes are or'ed with this
 * for example 0xff -> 0x100000ff
 */
#define U_INVALID_MASK 0x10000000U

/*
 * @uch  potential unicode character
 *
 * Returns 1 if @uch is valid unicode character, 0 otherwise
 */
static inline int u_is_unicode(uchar uch)
{
	return uch <= 0x0010ffffU;
}

/*
 * Returns size of @uch in bytes
 */
static inline unsigned int u_char_size(uchar uch)
{
	if (uch <= 0x0000007fU)
		return 1;
	if (uch <= 0x000007ffU)
		return 2;
	if (uch <= 0x0000ffffU)
		return 3;
	if (uch <= 0x0010ffffU)
		return 4;
	return 1;
}

static inline int u_seq_len(uchar first_byte)
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

static inline int u_is_continuation(uchar uch)
{
	return (uch & 0xc0) == 0x80;
}

static inline int u_seq_len_ok(uchar uch, int len)
{
	len--;
	return uch >= u_min_val[len] && uch <= u_max_val[len];
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

/*
 * Returns width of @uch (normally 1 or 2, 4 for invalid chars (<xx>))
 */
int u_char_width(uchar uch);

/*
 * @str  any null-terminated string
 *
 * Returns 1 if @str is valid UTF-8 string, 0 otherwise.
 */
int u_is_valid(const char *str);

/*
 * @str  null-terminated UTF-8 string
 *
 * Returns length of @str in UTF-8 characters.
 */
int u_strlen(const char *str);

/*
 * @str  null-terminated UTF-8 string
 *
 * Returns width of @str.
 */
unsigned int u_str_width(const char *str, unsigned int size);

uchar u_prev_char(const char *str, unsigned int *idx);
uchar u_buf_get_char(const char *buf, unsigned int size, unsigned int *idx);

/*
 * @str  destination buffer
 * @idx  pointer to byte index in @str (not UTF-8 character index!)
 * @uch  unicode character
 */
void u_set_char_raw(char *str, unsigned int *idx, uchar uch);
void u_set_char(char *str, unsigned int *idx, uchar uch);

/*
 * @dst    destination buffer
 * @src    null-terminated UTF-8 string
 * @width  how much to copy
 *
 * Copies at most @count characters, less if null byte was hit.
 * Null byte is _never_ copied.
 * Actual width of copied characters is stored to @width.
 *
 * Returns number of _bytes_ copied.
 */
int u_copy_chars(char *dst, const char *src, int *width);

/*
 * @str    null-terminated UTF-8 string, must be long enough
 * @width  how much to skip
 *
 * Skips @count UTF-8 characters.
 * Total width of skipped characters is stored to @width.
 * Returned @width can be the given @width + 1 if the last skipped
 * character was double width.
 *
 * Returns number of _bytes_ skipped.
 */
unsigned int u_skip_chars(const char *str, int *width);

int u_strcasecmp(const char *a, const char *b);
int u_strncasecmp(const char *a, const char *b, int len);
char *u_strcasestr(const char *haystack, const char *needle);

#endif
