#ifndef UCHAR_H
#define UCHAR_H

typedef unsigned int uchar;

extern const char hex_tab[16];
extern const signed char u_len_tab[256];
extern int u_min_val[4];
extern int u_max_val[4];
extern unsigned int u_first_byte_mask[4];

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
static inline int u_char_size(uchar uch)
{
	if (uch <= 0x0000007fU) {
		return 1;
	} else if (uch <= 0x000007ffU) {
		return 2;
	} else if (uch <= 0x0000ffffU) {
		return 3;
	} else if (uch <= 0x0010ffffU) {
		return 4;
	} else {
		return 1;
	}
}

/*
 * Returns width of @uch (normally 1 or 2, 4 for invalid chars (<xx>))
 */
extern int u_char_width(uchar uch);

/*
 * @str  any null-terminated string
 *
 * Returns 1 if @str is valid UTF-8 string, 0 otherwise.
 */
extern int u_is_valid(const char *str);

/*
 * @str  null-terminated UTF-8 string
 *
 * Returns length of @str in UTF-8 characters.
 */
extern int u_strlen(const char *str);

/*
 * @str  null-terminated UTF-8 string
 *
 * Returns width of @str.
 */
unsigned int u_str_width(const char *str);

uchar u_prev_char(const char *str, unsigned int *idx);
uchar u_buf_get_char(const char *buf, unsigned int size, unsigned int *idx);

/*
 * @str  destination buffer
 * @idx  pointer to byte index in @str (not UTF-8 character index!)
 * @uch  unicode character
 */
extern void u_set_char_raw(char *str, unsigned int *idx, uchar uch);
extern void u_set_char(char *str, unsigned int *idx, uchar uch);

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
extern int u_copy_chars(char *dst, const char *src, int *width);

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

extern int u_strcasecmp(const char *a, const char *b);
extern int u_strncasecmp(const char *a, const char *b, int len);
extern char *u_strcasestr(const char *haystack, const char *needle);

#endif
