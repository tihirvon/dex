#include "term.h"
#include "util.h"

/* booleans */
enum {
	tcb_auto_left_margin,
	tcb_auto_right_margin,
	tcb_no_esc_ctlc,
	tcb_ceol_standout_glitch,
	tcb_eat_newline_glitch,
	tcb_erase_overstrike,
	tcb_generic_type,
	tcb_hard_copy,
	tcb_has_meta_key,
	tcb_has_status_line,
	tcb_insert_null_glitch,
	tcb_memory_above,
	tcb_memory_below,
	tcb_move_insert_mode,
	tcb_move_standout_mode,
	tcb_over_strike,
	tcb_status_line_esc_ok,
	tcb_dest_tabs_magic_smso,
	tcb_tilde_glitch,
	tcb_transparent_underline,
	tcb_xon_xoff,
	tcb_needs_xon_xoff,
	tcb_prtr_silent,
	tcb_hard_cursor,
	tcb_non_rev_rmcup,
	tcb_no_pad_char,
	tcb_non_dest_scroll_region,
	tcb_can_change,
	tcb_back_color_erase,
	tcb_hue_lightness_saturation,
	tcb_col_addr_glitch,
	tcb_cr_cancels_micro_mode,
	tcb_has_print_wheel,
	tcb_row_addr_glitch,
	tcb_semi_auto_right_margin,
	tcb_cpi_changes_res,
	tcb_lpi_changes_res,

	tcb_backspaces_with_bs,
	tcb_crt_no_scrolling,
	tcb_no_correctly_working_cr,
	tcb_gnu_has_meta_key,
	tcb_linefeed_is_newline,
	tcb_has_hardware_tabs,
	tcb_return_does_clr_eol,
	nr_tcbs /* 44 */
};

/* numbers */
enum {
	tcn_columns,
	tcn_init_tabs,
	tcn_lines,
	tcn_lines_of_memory,
	tcn_magic_cookie_glitch,
	tcn_padding_baud_rate,
	tcn_virtual_terminal,
	tcn_width_status_line,
	tcn_num_labels,
	tcn_label_height,
	tcn_label_width,
	tcn_max_attributes,
	tcn_maximum_windows,
	tcn_max_colors,
	tcn_max_pairs,
	tcn_no_color_video,
	tcn_buffer_capacity,
	tcn_dot_vert_spacing,
	tcn_dot_horz_spacing,
	tcn_max_micro_address,
	tcn_max_micro_jump,
	tcn_micro_col_size,
	tcn_micro_line_size,
	tcn_number_of_pins,
	tcn_output_res_char,
	tcn_output_res_line,
	tcn_output_res_horz_inch,
	tcn_output_res_vert_inch,
	tcn_print_rate,
	tcn_wide_char_size,
	tcn_buttons,
	tcn_bit_image_entwining,
	tcn_bit_image_type,

	tcn_magic_cookie_glitch_ul,
	tcn_carriage_return_delay,
	tcn_new_line_delay,
	tcn_backspace_delay,
	tcn_horizontal_tab_delay,
	tcn_number_of_function_keys,
	nr_tcns /* 39 */
};

/* strings */
enum {
	tcs_back_tab,
	tcs_bell,
	tcs_carriage_return,
	tcs_change_scroll_region,
	tcs_clear_all_tabs,
	tcs_clear_screen,
	tcs_clr_eol,
	tcs_clr_eos,
	tcs_column_address,
	tcs_command_character,
	tcs_cursor_address,
	tcs_cursor_down,
	tcs_cursor_home,
	tcs_cursor_invisible,
	tcs_cursor_left,
	tcs_cursor_mem_address,
	tcs_cursor_normal,
	tcs_cursor_right,
	tcs_cursor_to_ll,
	tcs_cursor_up,
	tcs_cursor_visible,
	tcs_delete_character,
	tcs_delete_line,
	tcs_dis_status_line,
	tcs_down_half_line,
	tcs_enter_alt_charset_mode,
	tcs_enter_blink_mode,
	tcs_enter_bold_mode,
	tcs_enter_ca_mode,
	tcs_enter_delete_mode,
	tcs_enter_dim_mode,
	tcs_enter_insert_mode,
	tcs_enter_secure_mode,
	tcs_enter_protected_mode,
	tcs_enter_reverse_mode,
	tcs_enter_standout_mode,
	tcs_enter_underline_mode,
	tcs_erase_chars,
	tcs_exit_alt_charset_mode,
	tcs_exit_attribute_mode,
	tcs_exit_ca_mode,
	tcs_exit_delete_mode,
	tcs_exit_insert_mode,
	tcs_exit_standout_mode,
	tcs_exit_underline_mode,
	tcs_flash_screen,
	tcs_form_feed,
	tcs_from_status_line,
	tcs_init_1string,
	tcs_init_2string,
	tcs_init_3string,
	tcs_init_file,
	tcs_insert_character,
	tcs_insert_line,
	tcs_insert_padding,
	tcs_key_backspace,
	tcs_key_catab,
	tcs_key_clear,
	tcs_key_ctab,
	tcs_key_dc,
	tcs_key_dl,
	tcs_key_down,
	tcs_key_eic,
	tcs_key_eol,
	tcs_key_eos,
	tcs_key_f0,
	tcs_key_f1,
	tcs_key_f10,
	tcs_key_f2,
	tcs_key_f3,
	tcs_key_f4,
	tcs_key_f5,
	tcs_key_f6,
	tcs_key_f7,
	tcs_key_f8,
	tcs_key_f9,
	tcs_key_home,
	tcs_key_ic,
	tcs_key_il,
	tcs_key_left,
	tcs_key_ll,
	tcs_key_npage,
	tcs_key_ppage,
	tcs_key_right,
	tcs_key_sf,
	tcs_key_sr,
	tcs_key_stab,
	tcs_key_up,
	tcs_keypad_local,
	tcs_keypad_xmit,
	tcs_lab_f0,
	tcs_lab_f1,
	tcs_lab_f10,
	tcs_lab_f2,
	tcs_lab_f3,
	tcs_lab_f4,
	tcs_lab_f5,
	tcs_lab_f6,
	tcs_lab_f7,
	tcs_lab_f8,
	tcs_lab_f9,
	tcs_meta_off,
	tcs_meta_on,
	tcs_newline,
	tcs_pad_char,
	tcs_parm_dch,
	tcs_parm_delete_line,
	tcs_parm_down_cursor,
	tcs_parm_ich,
	tcs_parm_index,
	tcs_parm_insert_line,
	tcs_parm_left_cursor,
	tcs_parm_right_cursor,
	tcs_parm_rindex,
	tcs_parm_up_cursor,
	tcs_pkey_key,
	tcs_pkey_local,
	tcs_pkey_xmit,
	tcs_print_screen,
	tcs_prtr_off,
	tcs_prtr_on,
	tcs_repeat_char,
	tcs_reset_1string,
	tcs_reset_2string,
	tcs_reset_3string,
	tcs_reset_file,
	tcs_restore_cursor,
	tcs_row_address,
	tcs_save_cursor,
	tcs_scroll_forward,
	tcs_scroll_reverse,
	tcs_set_attributes,
	tcs_set_tab,
	tcs_set_window,
	tcs_tab,
	tcs_to_status_line,
	tcs_underline_char,
	tcs_up_half_line,
	tcs_init_prog,
	tcs_key_a1,
	tcs_key_a3,
	tcs_key_b2,
	tcs_key_c1,
	tcs_key_c3,
	tcs_prtr_non,
	tcs_char_padding,
	tcs_acs_chars,
	tcs_plab_norm,
	tcs_key_btab,
	tcs_enter_xon_mode,
	tcs_exit_xon_mode,
	tcs_enter_am_mode,
	tcs_exit_am_mode,
	tcs_xon_character,
	tcs_xoff_character,
	tcs_ena_acs,
	tcs_label_on,
	tcs_label_off,
	tcs_key_beg,
	tcs_key_cancel,
	tcs_key_close,
	tcs_key_command,
	tcs_key_copy,
	tcs_key_create,
	tcs_key_end,
	tcs_key_enter,
	tcs_key_exit,
	tcs_key_find,
	tcs_key_help,
	tcs_key_mark,
	tcs_key_message,
	tcs_key_move,
	tcs_key_next,
	tcs_key_open,
	tcs_key_options,
	tcs_key_previous,
	tcs_key_print,
	tcs_key_redo,
	tcs_key_reference,
	tcs_key_refresh,
	tcs_key_replace,
	tcs_key_restart,
	tcs_key_resume,
	tcs_key_save,
	tcs_key_suspend,
	tcs_key_undo,
	tcs_key_sbeg,
	tcs_key_scancel,
	tcs_key_scommand,
	tcs_key_scopy,
	tcs_key_screate,
	tcs_key_sdc,
	tcs_key_sdl,
	tcs_key_select,
	tcs_key_send,
	tcs_key_seol,
	tcs_key_sexit,
	tcs_key_sfind,
	tcs_key_shelp,
	tcs_key_shome,
	tcs_key_sic,
	tcs_key_sleft,
	tcs_key_smessage,
	tcs_key_smove,
	tcs_key_snext,
	tcs_key_soptions,
	tcs_key_sprevious,
	tcs_key_sprint,
	tcs_key_sredo,
	tcs_key_sreplace,
	tcs_key_sright,
	tcs_key_srsume,
	tcs_key_ssave,
	tcs_key_ssuspend,
	tcs_key_sundo,
	tcs_req_for_input,
	tcs_key_f11,
	tcs_key_f12,
	tcs_key_f13,
	tcs_key_f14,
	tcs_key_f15,
	tcs_key_f16,
	tcs_key_f17,
	tcs_key_f18,
	tcs_key_f19,
	tcs_key_f20,
	tcs_key_f21,
	tcs_key_f22,
	tcs_key_f23,
	tcs_key_f24,
	tcs_key_f25,
	tcs_key_f26,
	tcs_key_f27,
	tcs_key_f28,
	tcs_key_f29,
	tcs_key_f30,
	tcs_key_f31,
	tcs_key_f32,
	tcs_key_f33,
	tcs_key_f34,
	tcs_key_f35,
	tcs_key_f36,
	tcs_key_f37,
	tcs_key_f38,
	tcs_key_f39,
	tcs_key_f40,
	tcs_key_f41,
	tcs_key_f42,
	tcs_key_f43,
	tcs_key_f44,
	tcs_key_f45,
	tcs_key_f46,
	tcs_key_f47,
	tcs_key_f48,
	tcs_key_f49,
	tcs_key_f50,
	tcs_key_f51,
	tcs_key_f52,
	tcs_key_f53,
	tcs_key_f54,
	tcs_key_f55,
	tcs_key_f56,
	tcs_key_f57,
	tcs_key_f58,
	tcs_key_f59,
	tcs_key_f60,
	tcs_key_f61,
	tcs_key_f62,
	tcs_key_f63,
	tcs_clr_bol,
	tcs_clear_margins,
	tcs_set_left_margin,
	tcs_set_right_margin,
	tcs_label_format,
	tcs_set_clock,
	tcs_display_clock,
	tcs_remove_clock,
	tcs_create_window,
	tcs_goto_window,
	tcs_hangup,
	tcs_dial_phone,
	tcs_quick_dial,
	tcs_tone,
	tcs_pulse,
	tcs_flash_hook,
	tcs_fixed_pause,
	tcs_wait_tone,
	tcs_user0,
	tcs_user1,
	tcs_user2,
	tcs_user3,
	tcs_user4,
	tcs_user5,
	tcs_user6,
	tcs_user7,
	tcs_user8,
	tcs_user9,
	tcs_orig_pair,
	tcs_orig_colors,
	tcs_initialize_color,
	tcs_initialize_pair,
	tcs_set_color_pair,
	tcs_set_foreground,
	tcs_set_background,
	tcs_change_char_pitch,
	tcs_change_line_pitch,
	tcs_change_res_horz,
	tcs_change_res_vert,
	tcs_define_char,
	tcs_enter_doublewide_mode,
	tcs_enter_draft_quality,
	tcs_enter_italics_mode,
	tcs_enter_leftward_mode,
	tcs_enter_micro_mode,
	tcs_enter_near_letter_quality,
	tcs_enter_normal_quality,
	tcs_enter_shadow_mode,
	tcs_enter_subscript_mode,
	tcs_enter_superscript_mode,
	tcs_enter_upward_mode,
	tcs_exit_doublewide_mode,
	tcs_exit_italics_mode,
	tcs_exit_leftward_mode,
	tcs_exit_micro_mode,
	tcs_exit_shadow_mode,
	tcs_exit_subscript_mode,
	tcs_exit_superscript_mode,
	tcs_exit_upward_mode,
	tcs_micro_column_address,
	tcs_micro_down,
	tcs_micro_left,
	tcs_micro_right,
	tcs_micro_row_address,
	tcs_micro_up,
	tcs_order_of_pins,
	tcs_parm_down_micro,
	tcs_parm_left_micro,
	tcs_parm_right_micro,
	tcs_parm_up_micro,
	tcs_select_char_set,
	tcs_set_bottom_margin,
	tcs_set_bottom_margin_parm,
	tcs_set_left_margin_parm,
	tcs_set_right_margin_parm,
	tcs_set_top_margin,
	tcs_set_top_margin_parm,
	tcs_start_bit_image,
	tcs_start_char_set_def,
	tcs_stop_bit_image,
	tcs_stop_char_set_def,
	tcs_subscript_characters,
	tcs_superscript_characters,
	tcs_these_cause_cr,
	tcs_zero_motion,
	tcs_char_set_names,
	tcs_key_mouse,
	tcs_mouse_info,
	tcs_req_mouse_pos,
	tcs_get_mouse,
	tcs_set_a_foreground,
	tcs_set_a_background,
	tcs_pkey_plab,
	tcs_device_type,
	tcs_code_set_init,
	tcs_set0_des_seq,
	tcs_set1_des_seq,
	tcs_set2_des_seq,
	tcs_set3_des_seq,
	tcs_set_lr_margin,
	tcs_set_tb_margin,
	tcs_bit_image_repeat,
	tcs_bit_image_newline,
	tcs_bit_image_carriage_return,
	tcs_color_names,
	tcs_define_bit_image_region,
	tcs_end_bit_image_region,
	tcs_set_color_band,
	tcs_set_page_length,
	tcs_display_pc_char,
	tcs_enter_pc_charset_mode,
	tcs_exit_pc_charset_mode,
	tcs_enter_scancode_mode,
	tcs_exit_scancode_mode,
	tcs_pc_term_options,
	tcs_scancode_escape,
	tcs_alt_scancode_esc,
	tcs_enter_horizontal_hl_mode,
	tcs_enter_left_hl_mode,
	tcs_enter_low_hl_mode,
	tcs_enter_right_hl_mode,
	tcs_enter_top_hl_mode,
	tcs_enter_vertical_hl_mode,
	tcs_set_a_attributes,
	tcs_set_pglen_inch,

	tcs_termcap_init2,
	tcs_termcap_reset,
	tcs_linefeed_if_not_lf,
	tcs_backspace_if_not_bs,
	tcs_other_non_function_keys,
	tcs_arrow_key_map,
	tcs_acs_ulcorner,
	tcs_acs_llcorner,
	tcs_acs_urcorner,
	tcs_acs_lrcorner,
	tcs_acs_ltee,
	tcs_acs_rtee,
	tcs_acs_btee,
	tcs_acs_ttee,
	tcs_acs_hline,
	tcs_acs_vline,
	tcs_acs_plus,
	tcs_memory_lock,
	tcs_memory_unlock,
	tcs_box_chars_1,
	nr_tcss /* 414 */
};

static int keymap[NR_SKEYS] = {
	tcs_key_backspace,
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
	tcs_key_f12
};

static unsigned int nr_bools, nr_nums, nr_strs, strs_size;
static const char *bools;
static const char *nums;
static const char *offsets;
static const char *strs;

static inline int max(int a, int b)
{
	return a > b ? a : b;
}

static unsigned short get_u16le(const char *buffer)
{
	const unsigned char *buf = (const unsigned char *)buffer;

	return buf[0] + (buf[1] << 8);
}

static int get_bool(int idx)
{
	if (idx >= nr_bools)
		return 0;
	/*
	 * 0 absent
	 * 1 present
	 * 2 cancelled
	 */
	return bools[idx] == 1;
}

static int get_num(int idx)
{
	unsigned short val;

	if (idx >= nr_nums)
		return -1;
	val = get_u16le(nums + idx * 2);
	/*
	 * -1 missing
	 * -2 cancelled
	 */
	if (val >= 0xfffe)
		return -1;
	return val;
}

static char *get_str(int idx)
{
	unsigned short offset;

	if (idx >= nr_strs)
		return NULL;
	offset = get_u16le(offsets + idx * 2);
	/*
	 * -1 missing
	 * -2 cancelled
	 */
	if (offset >= 0xfffe)
		return NULL;
	return xstrdup(strs + offset);
}

static int validate(void)
{
	int valid = 1;
	int i;

	for (i = 0; i < nr_bools; i++) {
		if ((unsigned char)bools[i] > 2) {
			d_print("bool %3d: %d\n", i, bools[i]);
			valid = 0;
		}
	}

	for (i = 0; i < nr_nums; i++) {
		unsigned short num = get_u16le(nums + i * 2);
		if (num > 32767 && num < 0xfffe) {
			d_print("num %3d: negative\n", i);
			valid = 0;
		}
	}

	for (i = 0; i < nr_strs; i++) {
		unsigned short offset = get_u16le(offsets + i * 2);
		if (offset >= 0xfffe)
			continue;
		if (offset > 32767) {
			d_print("str %3d: negative\n", i);
			valid = 0;
		} else if (offset + 1 >= strs_size) {
			d_print("str %3d: invalid\n", i);
			valid = 0;
		} else {
			int len, max_size;

			max_size = strs_size - offset;
			for (len = 0; len < max_size && strs[offset + len]; len++)
				;
			if (len == max_size) {
				d_print("str %3d: missing NUL\n", i);
				valid = 0;
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
	struct stat st;
	char *buf;
	int fd, size, pos, i;
	int name_size, total_size;

	fd = open(filename, O_RDONLY);
	if (fd == -1)
		return -1;
	if (fstat(fd, &st) == -1) {
		close(fd);
		return -1;
	}
	size = st.st_size;
	buf = xmmap(fd, 0, size);
	close(fd);
	if (!buf)
		return -1;
	if (size < 12)
		goto corrupt;

	/* validate header */
	if (buf[0] != 0x1A || buf[1] != 0x01)
		return -2;

	name_size = get_u16le(buf + 2);
	nr_bools = get_u16le(buf + 4);
	nr_nums = get_u16le(buf + 6);
	nr_strs = get_u16le(buf + 8);
	strs_size = get_u16le(buf + 10);

	total_size = 12 + name_size + nr_bools + nr_nums * 2 + nr_strs * 2 + strs_size;
	if ((name_size + nr_bools) % 2)
		total_size++;

	// NOTE: size can be bigger than total_size if the format is extended
	if (total_size > size)
		goto corrupt;

	pos = 12 + name_size;
	bools = buf + pos;

	pos += nr_bools;
	if (pos % 2) {
		if (buf[pos])
			goto corrupt;
		pos++;
	}
	nums = buf + pos;

	pos += nr_nums * 2;
	offsets = buf + pos;

	pos += nr_strs * 2;
	strs = buf + pos;

	if (!validate())
		goto corrupt;

	/* now get only the interesting caps, ignore other crap */
	term_cap.ut = get_bool(tcb_back_color_erase);
	term_cap.colors = get_num(tcn_max_colors);
	term_cap.ce = get_str(tcs_clr_eol);
	term_cap.ks = get_str(tcs_keypad_xmit);
	term_cap.ke = get_str(tcs_keypad_local);
	term_cap.ti = get_str(tcs_enter_ca_mode);
	term_cap.te = get_str(tcs_exit_ca_mode);
	term_cap.vi = get_str(tcs_cursor_invisible);
	term_cap.ve = get_str(tcs_cursor_normal);
	for (i = 0; i < NR_SKEYS; i++)
		term_keycodes[i] = get_str(keymap[i]);

	xmunmap(buf, size);
	return 0;
corrupt:
	xmunmap(buf, size);
	return -2;
}
