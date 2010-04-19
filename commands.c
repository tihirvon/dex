#include "commands.h"
#include "editor.h"
#include "edit.h"
#include "move.h"
#include "window.h"
#include "change.h"
#include "term.h"
#include "search.h"
#include "cmdline.h"
#include "history.h"
#include "spawn.h"
#include "util.h"
#include "filetype.h"
#include "color.h"
#include "syntax.h"
#include "highlight.h"
#include "lock.h"
#include "ptr-array.h"
#include "bind.h"
#include "alias.h"
#include "tag.h"
#include "config.h"
#include "run.h"
#include "parse-args.h"
#include "file-option.h"

static int no_args(char **args)
{
	return !!parse_args(args, "", 0, 0);
}

static void cmd_alias(char **args)
{
	if (parse_args(args, "", 2, 2))
		add_alias(args[0], args[1]);
}

static void cmd_bind(char **args)
{
	if (parse_args(args, "", 2, 2))
		add_binding(args[0], args[1]);
}

static void cmd_bof(char **args)
{
	if (no_args(args))
		move_bof();
}

static void cmd_bol(char **args)
{
	if (no_args(args))
		move_bol();
}

static void cmd_cancel(char **args)
{
	if (no_args(args))
		select_end();
}

static void cmd_center_view(char **args)
{
	if (no_args(args))
		view->force_center = 1;
}

static void cmd_clear(char **args)
{
	if (no_args(args))
		clear_lines();
}

static void cmd_close(char **args)
{
	const char *pf = parse_args(args, "f", 0, 0);

	if (!pf)
		return;

	if (buffer_modified(buffer) && buffer->ref == 1 && !*pf) {
		error_msg("The buffer is modified. Save or run 'close -f' to close without saving.");
		return;
	}
	remove_view();
}

static void cmd_command(char **args)
{
	input_mode = INPUT_COMMAND;
	update_flags |= UPDATE_STATUS_LINE;

	if (args[0])
		cmdline_set_text(args[0]);
}

static void cmd_copy(char **args)
{
	struct block_iter save = view->cursor;

	if (!no_args(args))
		return;

	if (selecting()) {
		copy(prepare_selection(), view->selection == SELECT_LINES);
		select_end();
	} else {
		block_iter_bol(&view->cursor);
		copy(block_iter_count_to_next_line(&view->cursor), 1);
	}
	view->cursor = save;
}

static void cmd_cut(char **args)
{
	if (!no_args(args))
		return;

	if (selecting()) {
		cut(prepare_selection(), view->selection == SELECT_LINES);
		if (view->selection == SELECT_LINES)
			move_to_preferred_x();
		select_end();
	} else {
		block_iter_bol(&view->cursor);
		cut(block_iter_count_to_next_line(&view->cursor), 1);
		move_to_preferred_x();
	}
}

static void cmd_delete(char **args)
{
	if (no_args(args))
		delete_ch();
}

static void cmd_delete_eol(char **args)
{
	if (no_args(args)) {
		struct block_iter bi = view->cursor;
		delete(block_iter_eol(&bi), 0);
	}
}

static void cmd_delete_word(char **args)
{
	if (no_args(args)) {
		struct block_iter bi = view->cursor;
		delete(word_fwd(&bi), 0);
	}
}

static void cmd_down(char **args)
{
	if (no_args(args))
		move_down(1);
}

static void cmd_eof(char **args)
{
	if (no_args(args))
		move_eof();
}

static void cmd_eol(char **args)
{
	if (no_args(args))
		move_eol();
}

static void cmd_erase(char **args)
{
	if (no_args(args))
		erase();
}

static void cmd_erase_bol(char **args)
{
	if (no_args(args))
		delete(block_iter_bol(&view->cursor), 1);
}

static void cmd_erase_word(char **args)
{
	if (no_args(args))
		delete(word_bwd(&view->cursor), 1);
}

static void cmd_error(char **args)
{
	const char *pf = parse_args(args, "np", 0, 1);
	char dir = 0;

	if (!pf)
		return;

	while (*pf) {
		switch (*pf) {
		case 'n':
		case 'p':
			dir = *pf;
			break;
		}
		pf++;
	}

	if (!cerr.count) {
		info_msg("No errors");
		return;
	}
	if (dir && cerr.count == 1) {
		// this is much more useful than displaying "No more/previous errors"
		cerr.pos = 0;
		show_compile_error();
		return;
	}
	if (dir == 'n') {
		if (cerr.pos == cerr.count - 1) {
			info_msg("No more errors");
			return;
		}
		cerr.pos++;
	} else if (dir == 'p') {
		if (cerr.pos <= 0) {
			info_msg("No previous errors");
			return;
		}
		cerr.pos--;
	} else if (*args) {
		int num = atoi(*args);
		if (num < 1 || num > cerr.count) {
			info_msg("There are %d errors", cerr.count);
			return;
		}
		cerr.pos = num - 1;
	} else {
		// default is current error
		if (cerr.pos < 0)
			cerr.pos = 0;
	}
	show_compile_error();
}

static void cmd_errorfmt(char **args)
{
	const char *pf = parse_args(args, "ir", 2, -1);
	enum msg_importance importance = IMPORTANT;

	if (!pf)
		return;

	while (*pf) {
		switch (*pf) {
		case 'i':
			importance = USELESS;
			break;
		case 'r':
			importance = REDUNDANT;
			break;
		}
		pf++;
	}
	add_error_fmt(args[0], importance, args[1], args + 2);
}

static void cmd_ft_content(char **args)
{
	if (!parse_args(args, "", 2, 2))
		return;
	add_ft_content(args[0], args[1]);
}

static void cmd_ft_extension(char **args)
{
	if (!parse_args(args, "", 2, -1))
		return;
	add_ft_extensions(args[0], args + 1);
}

static void cmd_ft_match(char **args)
{
	if (!parse_args(args, "", 2, 2))
		return;
	add_ft_match(args[0], args[1]);
}

static const struct command ft_commands[] = {
	{ "content",	cmd_ft_content },
	{ "extension",	cmd_ft_extension },
	{ "match",	cmd_ft_match },
	{ NULL,		NULL }
};

static void cmd_ft(char **args)
{
	run_command(ft_commands, args);
}

static void cmd_filter(char **args)
{
	const char *pf = parse_args(args, "-", 1, -1);

	if (!pf)
		return;

	if (selecting()) {
		spawn_unfiltered_len = prepare_selection();
	} else {
		struct block *blk;

		spawn_unfiltered_len = 0;
		list_for_each_entry(blk, &buffer->blocks, node)
			spawn_unfiltered_len += blk->size;
		move_bof();
	}

	spawn_unfiltered = buffer_get_bytes(spawn_unfiltered_len);
	spawn(args, SPAWN_FILTER | SPAWN_PIPE_STDOUT | SPAWN_REDIRECT_STDERR, NULL);

	free(spawn_unfiltered);
	replace(spawn_unfiltered_len, spawn_filtered, spawn_filtered_len);
	free(spawn_filtered);

	select_end();
}

static void cmd_format_paragraph(char **args)
{
	const char *pf = parse_args(args, "", 0, 1);
	int text_width = buffer->options.text_width;

	if (!pf)
		return;
	if (args[0])
		text_width = atoi(args[0]);
	if (text_width <= 0) {
		error_msg("Paragraph width must be positive.");
		return;
	}
	format_paragraph(text_width);
}

static void cmd_hi(char **args)
{
	const char *pf = parse_args(args, "", 1, -1);
	struct term_color color;

	if (!pf)
		return;

	if (parse_term_color(&color, args + 1))
		set_highlight_color(args[0], &color);
}

static void cmd_include(char **args)
{
	if (!parse_args(args, "", 1, 1))
		return;
	read_config(args[0], 1);
}

static void cmd_insert(char **args)
{
	const char *pf = parse_args(args, "km", 1, 1);
	const char *str = args[0];

	if (!pf)
		return;

	if (selecting())
		delete_ch();

	if (strchr(pf, 'k')) {
		int i;
		for (i = 0; str[i]; i++)
			insert_ch(str[i]);
	} else {
		int len = strlen(str);

		insert(str, len);
		if (strchr(pf, 'm'))
			block_iter_skip_bytes(&view->cursor, len);
	}
}

static void cmd_insert_special(char **args)
{
	if (no_args(args)) {
		input_special = INPUT_SPECIAL_UNKNOWN;
		update_flags |= UPDATE_STATUS_LINE;
	}
}

static void cmd_join(char **args)
{
	if (no_args(args))
		join_lines();
}

static void cmd_left(char **args)
{
	if (no_args(args))
		move_cursor_left();
}

static void cmd_line(char **args)
{
	const char *pf = parse_args(args, "", 1, 1);
	int line;

	if (!pf)
		return;

	line = atoi(args[0]);
	if (line > 0) {
		move_to_line(line);
		move_to_preferred_x();
	}
}

static void cmd_load_syntax(char **args)
{
	const char *pf = parse_args(args, "", 1, 1);
	const char *name;

	if (!pf)
		return;

	name = args[0];
	if (!find_syntax(name))
		load_syntax(name);
}

static void cmd_move_tab(char **args)
{
	struct list_head *item;
	char *str, *end;
	long num;

	if (!parse_args(args, "", 1, 1))
		return;

	str = args[0];
	num = strtol(str, &end, 10);
	if (!*str || *end || num < 1)
		return error_msg("Invalid tab position %s", str);

	list_del(&view->node);
	item = &window->views;
	while (item->next != &window->views) {
		if (--num == 0)
			break;
		item = item->next;
	}
	list_add_after(&view->node, item);
	update_flags |= UPDATE_TAB_BAR;
}

static void cmd_new_line(char **args)
{
	if (no_args(args))
		new_line();
}

static void cmd_next(char **args)
{
	if (no_args(args))
		next_buffer();
}

static void cmd_open(char **args)
{
	const char *pf = parse_args(args, "", 0, -1);
	struct view *old_view = view;
	int i, first = 1;

	if (!pf)
		return;

	if (!args[0]) {
		set_view(open_empty_buffer());
		prev_view = old_view;
		return;
	}
	for (i = 0; args[i]; i++) {
		struct view *v = open_buffer(args[i], 0);
		if (v && first) {
			set_view(v);
			prev_view = old_view;
			first = 0;
		}
	}
}

static char **copy_string_array(char **src, int count)
{
	char **dst = xnew(char *, count + 1);
	int i;

	for (i = 0; i < count; i++)
		dst[i] = xstrdup(src[i]);
	dst[i] = NULL;
	return dst;
}

static void cmd_option_filename(char **args)
{
	const char *pf = parse_args(args, "", 3, -1);
	int argc = count_strings(args);

	if (!pf)
		return;

	if (argc % 2 == 0) {
		error_msg("Missing option value");
		return;
	}

	add_file_options(FILE_OPTIONS_FILENAME, xstrdup(args[0]), copy_string_array(args + 1, argc - 1));
}

static void cmd_option_filetype(char **args)
{
	const char *pf = parse_args(args, "", 3, -1);
	int argc = count_strings(args);
	char *list, *comma;
	char **strs;

	if (!pf)
		return;

	if (argc % 2 == 0) {
		error_msg("Missing option value");
		return;
	}

	// NOTE: options and values are shared
	strs = copy_string_array(args + 1, argc - 1);

	list = args[0];
	do {
		int len;

		comma = strchr(list, ',');
		len = comma ? comma - list : strlen(list);
		add_file_options(FILE_OPTIONS_FILETYPE, xstrndup(list, len), strs);
		list = comma + 1;
	} while (comma);
}

static const struct command option_commands[] = {
	{ "filename",	cmd_option_filename },
	{ "filetype",	cmd_option_filetype },
	{ NULL,		NULL }
};

static void cmd_option(char **args)
{
	run_command(option_commands, args);
}

static void cmd_pass_through(char **args)
{
	const char *pf = parse_args(args, "-s", 1, -1);
	unsigned int del_len = 0;
	int strip_nl;

	if (!pf)
		return;

	strip_nl = *pf == 's';

	spawn_unfiltered = NULL;
	spawn_unfiltered_len = 0;
	spawn(args, SPAWN_FILTER | SPAWN_PIPE_STDOUT | SPAWN_REDIRECT_STDERR, NULL);

	if (selecting()) {
		del_len = prepare_selection();
		select_end();
	}
	if (strip_nl && spawn_filtered_len > 0 && spawn_filtered[spawn_filtered_len - 1] == '\n') {
		if (--spawn_filtered_len > 0 && spawn_filtered[spawn_filtered_len - 1] == '\r')
			spawn_filtered_len--;
	}

	replace(del_len, spawn_filtered, spawn_filtered_len);
	free(spawn_filtered);
}

static void cmd_paste(char **args)
{
	if (no_args(args))
		paste();
}

static void cmd_pgdown(char **args)
{
	if (no_args(args))
		move_down(window->h - 1 - get_scroll_margin() * 2);
}

static void cmd_pgup(char **args)
{
	if (no_args(args))
		move_up(window->h - 1 - get_scroll_margin() * 2);
}

static void cmd_pop(char **args)
{
	if (no_args(args))
		pop_location();
}

static void cmd_prev(char **args)
{
	if (no_args(args))
		prev_buffer();
}

static void cmd_quit(char **args)
{
	const char *pf = parse_args(args, "f", 0, 0);
	struct window *w;
	struct view *v;

	if (!pf)
		return;
	if (pf[0]) {
		editor_status = EDITOR_EXITING;
		return;
	}
	list_for_each_entry(w, &windows, node) {
		list_for_each_entry(v, &w->views, node) {
			if (buffer_modified(v->buffer)) {
				error_msg("Save modified files or run 'quit -f' to quit without saving.");
				return;
			}
		}
	}
	editor_status = EDITOR_EXITING;
}

static void cmd_redo(char **args)
{
	const char *pf = parse_args(args, "", 0, 1);
	int change_id = 0;

	if (!pf)
		return;

	if (args[0]) {
		change_id = atoi(args[0]);
		if (change_id <= 0) {
			error_msg("Invalid change id: %s", args[0]);
			return;
		}
	}
	if (redo(change_id))
		select_end();
}

static void cmd_repeat(char **args)
{
	const char *pf = parse_args(args, "", 2, -1);
	const struct command *cmd;
	int count;

	if (!pf)
		return;

	count = atoi(args[0]);
	cmd = find_command(args[1]);
	if (!cmd) {
		error_msg("No such command: %s", args[1]);
		return;
	}
	while (count-- > 0)
		cmd->cmd(args + 2);
}

static void cmd_replace(char **args)
{
	const char *pf = parse_args(args, "bcgi", 2, 2);
	unsigned int flags = 0;
	int i;

	if (!pf)
		return;

	for (i = 0; pf[i]; i++) {
		switch (pf[i]) {
		case 'b':
			flags |= REPLACE_BASIC;
			break;
		case 'c':
			flags |= REPLACE_CONFIRM;
			break;
		case 'g':
			flags |= REPLACE_GLOBAL;
			break;
		case 'i':
			flags |= REPLACE_IGNORE_CASE;
			break;
		}
	}
	reg_replace(args[0], args[1], flags);
}

static void cmd_right(char **args)
{
	if (no_args(args))
		move_cursor_right();
}

static void cmd_run(char **args)
{
	const char *pf = parse_args(args, "-1cdef=ijps", 1, -1);
	const char *compiler = NULL;
	unsigned int flags = 0;
	int quoted = 0;

	if (!pf)
		return;

	while (*pf) {
		switch (*pf) {
		case '1':
			flags |= SPAWN_COLLECT_ERRORS | SPAWN_PIPE_STDOUT;
			break;
		case 'c':
			quoted = 1;
			break;
		case 'd':
			flags |= SPAWN_COLLECT_ERRORS | SPAWN_IGNORE_DUPLICATES;
			break;
		case 'e':
			flags |= SPAWN_COLLECT_ERRORS;
			break;
		case 'i':
			flags |= SPAWN_COLLECT_ERRORS | SPAWN_IGNORE_REDUNDANT;
			break;
		case 'j':
			flags |= SPAWN_COLLECT_ERRORS | SPAWN_JUMP_TO_ERROR;
			break;
		case 'p':
			flags |= SPAWN_PROMPT;
			break;
		case 's':
			flags |= SPAWN_REDIRECT_STDOUT | SPAWN_REDIRECT_STDERR;
			break;
		case 'f':
			compiler = *args++;
			break;
		}
		pf++;
	}

	if (compiler)
		flags |= SPAWN_COLLECT_ERRORS;

	if (flags & SPAWN_COLLECT_ERRORS && !(flags & SPAWN_PIPE_STDOUT))
		flags |= SPAWN_PIPE_STDERR;

	if (flags & SPAWN_COLLECT_ERRORS && !compiler) {
		error_msg("Error parser must be specified when collecting error messages.");
		return;
	}

	if (quoted) {
		PTR_ARRAY(array);

		if (args[1]) {
			error_msg("Too many arguments");
			return;
		}

		if (parse_commands(&array, args[0])) {
			ptr_array_free(&array);
			return;
		}
		spawn((char **)array.ptrs, flags, compiler);
		ptr_array_free(&array);
	} else {
		spawn(args, flags, compiler);
	}
}

static int stat_changed(const struct stat *a, const struct stat *b)
{
	/* don't compare st_mode because we allow chmod 755 etc. */
	return a->st_mtime != b->st_mtime ||
		a->st_dev != b->st_dev ||
		a->st_ino != b->st_ino;
}

static void cmd_save(char **args)
{
	const char *pf = parse_args(args, "dfu", 0, 1);
	char *absolute = buffer->abs_filename;
	int force = 0;
	enum newline_sequence newline = buffer->newline;
	mode_t old_mode = buffer->st.st_mode;
	struct stat st;
	int new_locked = 0;

	if (!pf)
		return;

	while (*pf) {
		switch (*pf) {
		case 'd':
			newline = NEWLINE_DOS;
			break;
		case 'f':
			force = 1;
			break;
		case 'u':
			newline = NEWLINE_UNIX;
			break;
		}
		pf++;
	}

	if (args[0]) {
		char *tmp = path_absolute(args[0]);
		if (!tmp) {
			error_msg("Failed to make absolute path: %s", strerror(errno));
			return;
		}
		if (absolute && !strcmp(tmp, absolute)) {
			free(tmp);
		} else {
			absolute = tmp;
		}
	} else {
		if (!absolute) {
			error_msg("No filename.");
			return;
		}
		if (buffer->ro && !force) {
			error_msg("Use -f to force saving read-only file.");
			return;
		}
	}

	if (stat(absolute, &st)) {
		if (errno != ENOENT) {
			error_msg("stat failed for %s: %s", absolute, strerror(errno));
			goto error;
		}
		if (options.lock_files) {
			if (absolute == buffer->abs_filename) {
				if (!buffer->locked) {
					if (lock_file(absolute)) {
						if (!force) {
							error_msg("Can't lock file %s", absolute);
							goto error;
						}
					} else {
						buffer->locked = 1;
					}
				}
			} else {
				if (lock_file(absolute)) {
					if (!force) {
						error_msg("Can't lock file %s", absolute);
						goto error;
					}
				} else {
					new_locked = 1;
				}
			}
		}
	} else {
		if (absolute == buffer->abs_filename && !force && stat_changed(&buffer->st, &st)) {
			error_msg("File has been modified by someone else. Use -f to force overwrite.");
			goto error;
		}
		if (S_ISDIR(st.st_mode)) {
			error_msg("Will not overwrite directory %s", absolute);
			goto error;
		}
		if (options.lock_files) {
			if (absolute == buffer->abs_filename) {
				if (!buffer->locked) {
					if (lock_file(absolute)) {
						if (!force) {
							error_msg("Can't lock file %s", absolute);
							goto error;
						}
					} else {
						buffer->locked = 1;
					}
				}
			} else {
				if (lock_file(absolute)) {
					if (!force) {
						error_msg("Can't lock file %s", absolute);
						goto error;
					}
				} else {
					new_locked = 1;
				}
			}
		}
		if (absolute != buffer->abs_filename && !force) {
			error_msg("Use -f to overwrite %s %s.", get_file_type(st.st_mode), absolute);
			goto error;
		}

		/* allow chmod 755 etc. */
		buffer->st.st_mode = st.st_mode;
	}
	if (save_buffer(absolute, newline))
		goto error;

	if (absolute != buffer->abs_filename) {
		if (buffer->locked) {
			// filename changes, relase old file lock
			unlock_file(buffer->abs_filename);
		}
		buffer->locked = new_locked;

		free(buffer->filename);
		free(buffer->abs_filename);
		buffer->filename = xstrdup(args[0]);
		buffer->abs_filename = absolute;
	}
	if (!old_mode && !strcmp(buffer->options.filetype, "none")) {
		/* new file and most likely user has not changed the filetype */
		if (guess_filetype())
			filetype_changed();
	}
	return;
error:
	if (new_locked)
		unlock_file(absolute);
	if (absolute != buffer->abs_filename)
		free(absolute);
}

static void do_search_next(char **args, enum search_direction dir)
{
	const char *pf = parse_args(args, "w", 0, 0);

	if (!pf)
		return;

	if (*pf) {
		char *pattern, *word = get_word_under_cursor();

		if (!word) {
			return;
		}
		pattern = xnew(char, strlen(word) + 5);
		sprintf(pattern, "\\<%s\\>", word);
		search_init(dir);
		search(pattern);
		history_add(&search_history, pattern);
		free(word);
		free(pattern);
	} else {
		input_mode = INPUT_SEARCH;
		search_init(dir);
		update_flags |= UPDATE_STATUS_LINE;
	}
}

static void cmd_scroll_pgdown(char **args)
{
	int max = buffer->nl - window->h + 1;

	if (no_args(args) && view->vy < max && max > 0) {
		int count = window->h - 1;

		if (view->vy + count > max)
			count = max - view->vy;
		view->vy += count;
		move_down(count);
		update_flags |= UPDATE_FULL;
	}
}

static void cmd_scroll_pgup(char **args)
{
	if (no_args(args) && view->vy > 0) {
		int count = window->h - 1;

		if (count > view->vy)
			count = view->vy;
		view->vy -= count;
		move_up(count);
		update_flags |= UPDATE_FULL;
	}
}

static void cmd_search_bwd(char **args)
{
	do_search_next(args, SEARCH_BWD);
}

static void cmd_search_fwd(char **args)
{
	do_search_next(args, SEARCH_FWD);
}

static void cmd_search_next(char **args)
{
	if (no_args(args))
		search_next();
}

static void cmd_search_prev(char **args)
{
	if (no_args(args))
		search_prev();
}

static void cmd_select(char **args)
{
	const char *pf = parse_args(args, "l", 0, 0);
	int sel = SELECT_CHARS;

	if (!pf)
		return;

	if (*pf)
		sel = SELECT_LINES;

	if (selecting()) {
		if (view->selection == sel) {
			select_end();
			return;
		}
		view->selection = sel;
		update_flags |= UPDATE_FULL;
		return;
	}

	view->sel_so = block_iter_get_offset(&view->cursor);
	view->sel_eo = UINT_MAX;
	view->selection = sel;
	update_flags |= UPDATE_CURSOR_LINE;
}

static void cmd_set(char **args)
{
	const char *pf = parse_args(args, "gl", 1, 2);
	unsigned int flags = 0;

	if (!pf)
		return;

	while (*pf) {
		switch (*pf) {
		case 'g':
			flags |= OPT_GLOBAL;
			break;
		case 'l':
			flags |= OPT_LOCAL;
			break;
		}
		pf++;
	}
	set_option(args[0], args[1], flags);
}

static void cmd_shift(char **args)
{
	const char *pf = parse_args(args, "", 0, 1);
	int count = 1;

	if (!pf)
		return;
	if (args[0])
		count = atoi(args[0]);
	if (!count) {
		error_msg("Count must be non-zero.");
		return;
	}
	shift_lines(count);
}

static const struct command syn_commands[] = {
	{ "addc",	syn_addc },
	{ "addr",	syn_addr },
	{ "addw",	syn_addw },
	{ "begin",	syn_begin },
	{ "connect",	syn_connect },
	{ "end",	syn_end },
	{ "join",	syn_join },
	{ NULL,		NULL }
};

static void cmd_syn(char **args)
{
	run_command(syn_commands, args);
}

static void goto_tag(int pos, int save_location)
{
	if (current_tags.count > 1)
		info_msg("[%d/%d]", pos + 1, current_tags.count);
	move_to_tag(current_tags.ptrs[pos], save_location);
}

static void cmd_tag(char **args)
{
	const char *pf = parse_args(args, "np", 0, 1);
	static int pos;
	char dir = 0;

	if (!pf)
		return;

	while (*pf) {
		switch (*pf) {
		case 'n':
		case 'p':
			dir = *pf;
			break;
		}
		pf++;
	}

	if (!dir) {
		const char *name = args[0];
		char *word = NULL;

		if (!name) {
			word = get_word_under_cursor();
			if (!word)
				return;
			name = word;
		}

		pos = 0;
		if (!find_tags(name)) {
			error_msg("No tag file.");
		} else if (!current_tags.count) {
			error_msg("Tag %s not found.", name);
		} else {
			goto_tag(pos, 1);
		}
		free(word);
		return;
	}

	if (!current_tags.count) {
		error_msg("No tags.");
		return;
	}

	if (dir == 'n') {
		if (pos + 1 >= current_tags.count) {
			error_msg("No more tags.");
			return;
		}
		goto_tag(++pos, 0);
	} else if (dir == 'p') {
		if (!pos) {
			error_msg("At first tag.");
			return;
		}
		goto_tag(--pos, 0);
	}
}

static void cmd_toggle(char **args)
{
	const char *pf = parse_args(args, "gl", 1, 1);
	unsigned int flags = 0;

	if (!pf)
		return;

	while (*pf) {
		switch (*pf) {
		case 'g':
			flags |= OPT_GLOBAL;
			break;
		case 'l':
			flags |= OPT_LOCAL;
			break;
		}
		pf++;
	}
	toggle_option(args[0], flags);
}

static void cmd_undo(char **args)
{
	if (no_args(args)) {
		if (undo())
			select_end();
	}
}

static void cmd_up(char **args)
{
	if (no_args(args))
		move_up(1);
}

static void cmd_view(char **args)
{
	const char *pf = parse_args(args, "", 1, 1);
	struct list_head *node;
	int idx;

	if (!pf)
		return;

	idx = atoi(args[0]) - 1;
	if (idx < 0) {
		error_msg("View number must be positive.");
		return;
	}

	node = window->views.next;
	while (node != &window->views) {
		if (!idx--) {
			set_view(VIEW(node));
			return;
		}
		node = node->next;
	}
}

static void cmd_word_bwd(char **args)
{
	if (no_args(args)) {
		word_bwd(&view->cursor);
		update_preferred_x();
	}
}

static void cmd_word_fwd(char **args)
{
	if (no_args(args)) {
		word_fwd(&view->cursor);
		update_preferred_x();
	}
}

const struct command commands[] = {
	{ "alias",		cmd_alias },
	{ "bind",		cmd_bind },
	{ "bof",		cmd_bof },
	{ "bol",		cmd_bol },
	{ "cancel",		cmd_cancel },
	{ "center-view",	cmd_center_view },
	{ "clear",		cmd_clear },
	{ "close",		cmd_close },
	{ "command",		cmd_command },
	{ "copy",		cmd_copy },
	{ "cut",		cmd_cut },
	{ "delete",		cmd_delete },
	{ "delete-eol",		cmd_delete_eol },
	{ "delete-word",	cmd_delete_word },
	{ "down",		cmd_down },
	{ "eof",		cmd_eof },
	{ "eol",		cmd_eol },
	{ "erase",		cmd_erase },
	{ "erase-bol",		cmd_erase_bol },
	{ "erase-word",		cmd_erase_word },
	{ "error",		cmd_error },
	{ "errorfmt",		cmd_errorfmt },
	{ "filter",		cmd_filter },
	{ "format-paragraph",	cmd_format_paragraph },
	{ "ft",			cmd_ft },
	{ "hi",			cmd_hi },
	{ "include",		cmd_include },
	{ "insert",		cmd_insert },
	{ "insert-special",	cmd_insert_special },
	{ "join",		cmd_join },
	{ "left",		cmd_left },
	{ "line",		cmd_line },
	{ "load-syntax",	cmd_load_syntax },
	{ "move-tab",		cmd_move_tab },
	{ "new-line",		cmd_new_line },
	{ "next",		cmd_next },
	{ "open",		cmd_open },
	{ "option",		cmd_option },
	{ "pass-through",	cmd_pass_through },
	{ "paste",		cmd_paste },
	{ "pgdown",		cmd_pgdown },
	{ "pgup",		cmd_pgup },
	{ "pop",		cmd_pop },
	{ "prev",		cmd_prev },
	{ "quit",		cmd_quit },
	{ "redo",		cmd_redo },
	{ "repeat",		cmd_repeat },
	{ "replace",		cmd_replace },
	{ "right",		cmd_right },
	{ "run",		cmd_run },
	{ "save",		cmd_save },
	{ "scroll-pgdown",	cmd_scroll_pgdown },
	{ "scroll-pgup",	cmd_scroll_pgup },
	{ "search-bwd",		cmd_search_bwd },
	{ "search-fwd",		cmd_search_fwd },
	{ "search-next",	cmd_search_next },
	{ "search-prev",	cmd_search_prev },
	{ "select",		cmd_select },
	{ "set",		cmd_set },
	{ "shift",		cmd_shift },
	{ "syn",		cmd_syn },
	{ "tag",		cmd_tag },
	{ "toggle",		cmd_toggle },
	{ "undo",		cmd_undo },
	{ "up",			cmd_up },
	{ "view",		cmd_view },
	{ "word-bwd",		cmd_word_bwd },
	{ "word-fwd",		cmd_word_fwd },
	{ NULL,			NULL }
};
