#include "term.h"
#include "common.h"
#include "terminfo-enum.h"

static unsigned char string_cap_map[NR_STR_CAPS] = {
	tcs_acs_chars,
	tcs_exit_alt_charset_mode,
	tcs_enter_alt_charset_mode,
	tcs_clr_eol,
	tcs_keypad_local,
	tcs_keypad_xmit,
	tcs_exit_ca_mode,
	tcs_enter_ca_mode,
	tcs_cursor_normal,
	tcs_cursor_invisible,
};

static unsigned char key_cap_map[NR_SPECIAL_KEYS] = {
	tcs_key_ic,
	tcs_key_dc,
	tcs_key_home,
	tcs_key_end,
	tcs_key_ppage,
	tcs_key_npage,
	tcs_key_left,
	tcs_key_right,
	tcs_key_up,
	tcs_key_down,

	tcs_key_f1,
	tcs_key_f2,
	tcs_key_f3,
	tcs_key_f4,
	tcs_key_f5,
	tcs_key_f6,
	tcs_key_f7,
	tcs_key_f8,
	tcs_key_f9,
	tcs_key_f10,
	tcs_key_f11,
	tcs_key_f12,
};

struct terminfo {
	unsigned int nr_bools, nr_nums, nr_strs, strs_size;
	const unsigned char *bools;
	const unsigned char *nums;
	const unsigned char *offsets;
	const char *strs;
};

static inline int max(int a, int b)
{
	return a > b ? a : b;
}

static unsigned short get_u16le(const unsigned char *buf)
{
	return buf[0] + (buf[1] << 8);
}

static bool get_bool(struct terminfo *ti, int idx)
{
	if (idx >= ti->nr_bools)
		return false;
	/*
	 * 0 absent
	 * 1 present
	 * 2 cancelled
	 */
	return ti->bools[idx] == 1;
}

static int get_num(struct terminfo *ti, int idx)
{
	unsigned short val;

	if (idx >= ti->nr_nums)
		return -1;
	val = get_u16le(ti->nums + idx * 2);
	/*
	 * -1 missing
	 * -2 cancelled
	 */
	if (val >= 0xfffe)
		return -1;
	return val;
}

static char *get_str(struct terminfo *ti, int idx)
{
	unsigned short offset;

	if (idx >= ti->nr_strs)
		return NULL;
	offset = get_u16le(ti->offsets + idx * 2);
	/*
	 * -1 missing
	 * -2 cancelled
	 */
	if (offset >= 0xfffe)
		return NULL;
	return xstrdup(ti->strs + offset);
}

static bool validate(struct terminfo *ti)
{
	bool valid = true;
	int i;

	for (i = 0; i < ti->nr_bools; i++) {
		if (ti->bools[i] > 2) {
			d_print("bool %3d: %d\n", i, ti->bools[i]);
			valid = false;
		}
	}

	for (i = 0; i < ti->nr_nums; i++) {
		unsigned short num = get_u16le(ti->nums + i * 2);
		if (num > 32767 && num < 0xfffe) {
			d_print("num %3d: negative\n", i);
			valid = false;
		}
	}

	for (i = 0; i < ti->nr_strs; i++) {
		unsigned short offset = get_u16le(ti->offsets + i * 2);
		if (offset >= 0xfffe)
			continue;
		if (offset > 32767) {
			d_print("str %3d: negative\n", i);
			valid = false;
		} else if (offset + 1 >= ti->strs_size) {
			d_print("str %3d: invalid\n", i);
			valid = false;
		} else {
			int len, max_size;

			max_size = ti->strs_size - offset;
			for (len = 0; len < max_size && ti->strs[offset + len]; len++)
				;
			if (len == max_size) {
				d_print("str %3d: missing NUL\n", i);
				valid = false;
			}
		}
	}
	return valid;
}

/* terminfo format (see man 5 term):
 *
 *  0 1 0x1A
 *  1 1 0x01
 *  2 2 name size   (A, max 128)
 *  4 2 nr booleans (B)
 *  6 2 nr numbers  (N)
 *  8 2 nr strings  (S)
 * 10 2 string table size (T)
 * 12 A names
 *
 *  12 + A                 B     booleans (0 or 1)
 * (12 + A + B + 1) & ~1U  N * 2 numbers
 *                         S * 2 string offsets
 *                         T     string table
 */
int terminfo_get_caps(const char *filename)
{
	struct terminfo ti;
	char *buf;
	ssize_t size, pos;
	int i, name_size, total_size;
	struct term_keymap *km;

	size = read_file(filename, &buf);
	if (size < 0)
		return -1;

	/* validate header */
	if (size < 12 || buf[0] != 0x1a || buf[1] != 0x01)
		goto corrupt;

	name_size = get_u16le(buf + 2);
	ti.nr_bools = get_u16le(buf + 4);
	ti.nr_nums = get_u16le(buf + 6);
	ti.nr_strs = get_u16le(buf + 8);
	ti.strs_size = get_u16le(buf + 10);

	total_size = 12 + name_size + ti.nr_bools + ti.nr_nums * 2 + ti.nr_strs * 2 + ti.strs_size;
	total_size += (name_size + ti.nr_bools) % 2;

	// NOTE: size can be bigger than total_size if the format is extended
	if (total_size > size)
		goto corrupt;

	pos = 12 + name_size;
	ti.bools = buf + pos;

	pos += ti.nr_bools;
	if (pos % 2) {
		if (buf[pos])
			goto corrupt;
		pos++;
	}
	ti.nums = buf + pos;

	pos += ti.nr_nums * 2;
	ti.offsets = buf + pos;

	pos += ti.nr_strs * 2;
	ti.strs = buf + pos;

	if (!validate(&ti))
		goto corrupt;

	/* now get only the interesting caps, ignore other crap */
	term_cap.ut = get_bool(&ti, tcb_back_color_erase);
	term_cap.colors = get_num(&ti, tcn_max_colors);
	for (i = 0; i < NR_STR_CAPS; i++) {
		term_cap.strings[i] = get_str(&ti, string_cap_map[i]);
	}

	term_cap.keymap_size = NR_SPECIAL_KEYS + 2;
	term_cap.keymap = xnew(struct term_keymap, term_cap.keymap_size);
	for (i = 0; i < NR_SPECIAL_KEYS; i++) {
		km = &term_cap.keymap[i];
		km->key = KEY_SPECIAL_MIN + i;
		km->code = get_str(&ti, key_cap_map[i]);
	}
	km = &term_cap.keymap[i];
	km->key = MOD_SHIFT | KEY_LEFT;
	km->code = get_str(&ti, tcs_key_sleft);
	i++;
	km = &term_cap.keymap[i];
	km->key = MOD_SHIFT | KEY_RIGHT;
	km->code = get_str(&ti, tcs_key_sright);

	free(buf);
	return 0;
corrupt:
	free(buf);
	return -2;
}
