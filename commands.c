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
#include "state.h"
#include "lock.h"
#include "ptr-array.h"
#include "bind.h"
#include "alias.h"
#include "tag.h"
#include "config.h"
#include "command.h"
#include "parse-args.h"
#include "file-option.h"

static void cmd_alias(const char *pf, char **args)
{
	add_alias(args[0], args[1]);
}

static void cmd_bind(const char *pf, char **args)
{
	add_binding(args[0], args[1]);
}

static void cmd_bof(const char *pf, char **args)
{
	move_bof();
}

static void cmd_bol(const char *pf, char **args)
{
	move_bol();
}

static void cmd_case(const char *pf, char **args)
{
	int mode = 't';
	int move = 0;

	while (*pf) {
		switch (*pf) {
		case 'l':
		case 'u':
			mode = *pf;
			break;
		case 'm':
			move = 1;
			break;
		}
		pf++;
	}

	change_case(mode, move);
}

static void cmd_cd(const char *pf, char **args)
{
	char cwd[PATH_MAX];
	struct window *w;
	struct view *v;
	int got_cwd;

	if (chdir(args[0])) {
		error_msg("cd: %s", strerror(errno));
		return;
	}

	got_cwd = !!getcwd(cwd, sizeof(cwd));
	list_for_each_entry(w, &windows, node) {
		list_for_each_entry(v, &w->views, node) {
			if (got_cwd)
				update_short_filename_cwd(v->buffer, cwd);
			else
				v->buffer->filename = xstrdup(v->buffer->abs_filename);
		}
	}

	update_flags |= UPDATE_TAB_BAR;
}

static void cmd_center_view(const char *pf, char **args)
{
	view->force_center = 1;
}

static void cmd_clear(const char *pf, char **args)
{
	clear_lines();
}

static void cmd_close(const char *pf, char **args)
{
	if (buffer_modified(buffer) && buffer->ref == 1 && !*pf) {
		error_msg("The buffer is modified. Save or run 'close -f' to close without saving.");
		return;
	}
	remove_view();
}

static void cmd_command(const char *pf, char **args)
{
	input_mode = INPUT_COMMAND;
	update_flags |= UPDATE_COMMAND_LINE;

	if (args[0])
		cmdline_set_text(args[0]);
}

static void cmd_copy(const char *pf, char **args)
{
	struct block_iter save = view->cursor;

	if (selecting()) {
		copy(prepare_selection(), view->selection == SELECT_LINES);
		unselect();
	} else {
		block_iter_bol(&view->cursor);
		copy(block_iter_count_to_next_line(&view->cursor), 1);
	}
	view->cursor = save;
}

static void cmd_cut(const char *pf, char **args)
{
	if (selecting()) {
		cut(prepare_selection(), view->selection == SELECT_LINES);
		if (view->selection == SELECT_LINES)
			move_to_preferred_x();
		unselect();
	} else {
		block_iter_bol(&view->cursor);
		cut(block_iter_count_to_next_line(&view->cursor), 1);
		move_to_preferred_x();
	}
}

static void cmd_delete(const char *pf, char **args)
{
	delete_ch();
}

static void cmd_delete_eol(const char *pf, char **args)
{
	struct block_iter bi = view->cursor;
	delete(block_iter_eol(&bi), 0);
}

static void cmd_delete_word(const char *pf, char **args)
{
	int skip_non_word = *pf == 's';
	struct block_iter bi = view->cursor;
	delete(word_fwd(&bi, skip_non_word), 0);
}

static void cmd_down(const char *pf, char **args)
{
	move_down(1);
}

static void cmd_eof(const char *pf, char **args)
{
	move_eof();
}

static void cmd_eol(const char *pf, char **args)
{
	move_eol();
}

static void cmd_erase(const char *pf, char **args)
{
	erase();
}

static void cmd_erase_bol(const char *pf, char **args)
{
	delete(block_iter_bol(&view->cursor), 1);
}

static void cmd_erase_word(const char *pf, char **args)
{
	int skip_non_word = *pf == 's';
	delete(word_bwd(&view->cursor, skip_non_word), 1);
}

static void cmd_error(const char *pf, char **args)
{
	char dir = 0;

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

static void cmd_errorfmt(const char *pf, char **args)
{
	enum msg_importance importance = IMPORTANT;

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

static void cmd_ft(const char *pf, char **args)
{
	enum detect_type dt = FT_EXTENSION;
	int i;

	while (*pf) {
		switch (*pf) {
		case 'c':
			dt = FT_CONTENT;
			break;
		case 'f':
			dt = FT_FILENAME;
			break;
		case 'i':
			dt = FT_INTERPRETER;
			break;
		}
		pf++;
	}
	for (i = 1; args[i]; i++)
		add_filetype(args[0], args[i], dt);
}

static void cmd_filter(const char *pf, char **args)
{
	struct filter_data data;
	struct block_iter save = view->cursor;

	if (selecting()) {
		data.in_len = prepare_selection();
	} else {
		struct block *blk;

		data.in_len = 0;
		list_for_each_entry(blk, &buffer->blocks, node)
			data.in_len += blk->size;
		move_bof();
	}

	data.in = buffer_get_bytes(data.in_len);
	if (spawn_filter(args, &data)) {
		free(data.in);
		view->cursor = save;
		return;
	}

	free(data.in);
	replace(data.in_len, data.out, data.out_len);
	free(data.out);

	unselect();
}

static void cmd_format_paragraph(const char *pf, char **args)
{
	int text_width = buffer->options.text_width;

	if (args[0])
		text_width = atoi(args[0]);
	if (text_width <= 0) {
		error_msg("Paragraph width must be positive.");
		return;
	}
	format_paragraph(text_width);
}

static void cmd_hi(const char *pf, char **args)
{
	struct term_color color;

	if (parse_term_color(&color, args + 1)) {
		set_highlight_color(args[0], &color);

		// Don't call update_all_syntax_colors() needlessly.
		// It is called right after config has been loaded.
		if (editor_status != EDITOR_INITIALIZING) {
			update_all_syntax_colors();
			update_flags = UPDATE_FULL;
		}
	}
}

static void cmd_include(const char *pf, char **args)
{
	read_config(commands, args[0], 1);
}

static void cmd_insert(const char *pf, char **args)
{
	const char *str = args[0];

	if (strchr(pf, 'k')) {
		int i;
		for (i = 0; str[i]; i++)
			insert_ch(str[i]);
	} else {
		unsigned int del_len = 0;
		unsigned int ins_len = strlen(str);

		if (selecting()) {
			del_len = prepare_selection();
			unselect();
		}

		replace(del_len, str, ins_len);
		if (strchr(pf, 'm'))
			block_iter_skip_bytes(&view->cursor, ins_len);
	}
}

static void cmd_insert_special(const char *pf, char **args)
{
	input_special = INPUT_SPECIAL_UNKNOWN;
}

static void cmd_join(const char *pf, char **args)
{
	join_lines();
}

static void cmd_left(const char *pf, char **args)
{
	move_cursor_left();
}

static void cmd_line(const char *pf, char **args)
{
	int line;

	line = atoi(args[0]);
	if (line > 0) {
		move_to_line(line);
		move_to_preferred_x();
	}
}

static void cmd_load_syntax(const char *pf, char **args)
{
	const char *slash = strrchr(args[0], '/');
	const char *filename = slash ? args[0] : NULL;
	const char *filetype = slash ? slash + 1 : args[0];

	if (filename) {
		if (find_syntax(filetype)) {
			error_msg("Syntax for filetype %s already loaded", filetype);
		} else {
			load_syntax_by_filename(filename);
		}
	} else {
		if (!find_syntax(filetype))
			load_syntax_by_filetype(filetype);
	}
}

static void cmd_move_tab(const char *pf, char **args)
{
	struct list_head *item;
	char *end, *str = args[0];
	long num;

	if (!strcmp(str, "left")) {
		item = view->node.prev;
		list_del(&view->node);
		list_add_before(&view->node, item);
		update_flags |= UPDATE_TAB_BAR;
		return;
	}
	if (!strcmp(str, "right")) {
		item = view->node.next;
		list_del(&view->node);
		list_add_after(&view->node, item);
		update_flags |= UPDATE_TAB_BAR;
		return;
	}
	num = strtol(str, &end, 10);
	if (!*str || *end || num < 1) {
		error_msg("Invalid tab position %s", str);
		return;
	}

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

static void cmd_new_line(const char *pf, char **args)
{
	new_line();
}

static void cmd_next(const char *pf, char **args)
{
	next_buffer();
}

static void cmd_open(const char *pf, char **args)
{
	struct view *old_view = view;
	int i, first = 1;

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

static void cmd_option(const char *pf, char **args)
{
	int argc = count_strings(args);
	char *list, *comma;
	char **strs;

	if (argc % 2 == 0) {
		error_msg("Missing option value");
		return;
	}

	// NOTE: options and values are shared
	strs = copy_string_array(args + 1, argc - 1);

	if (*pf) {
		add_file_options(FILE_OPTIONS_FILENAME, xstrdup(args[0]), strs);
		return;
	}

	list = args[0];
	do {
		int len;

		comma = strchr(list, ',');
		len = comma ? comma - list : strlen(list);
		add_file_options(FILE_OPTIONS_FILETYPE, xstrndup(list, len), strs);
		list = comma + 1;
	} while (comma);
}

static void cmd_pass_through(const char *pf, char **args)
{
	struct filter_data data;
	unsigned int del_len = 0;
	int strip_nl = 0;
	int move = 0;

	while (*pf) {
		switch (*pf++) {
		case 'm':
			move = 1;
			break;
		case 's':
			strip_nl = 1;
			break;
		}
	}

	data.in = NULL;
	data.in_len = 0;
	if (spawn_filter(args, &data))
		return;

	if (selecting()) {
		del_len = prepare_selection();
		unselect();
	}
	if (strip_nl && data.out_len > 0 && data.out[data.out_len - 1] == '\n') {
		if (--data.out_len > 0 && data.out[data.out_len - 1] == '\r')
			data.out_len--;
	}

	replace(del_len, data.out, data.out_len);
	free(data.out);

	if (move) {
		block_iter_skip_bytes(&view->cursor, data.out_len);
		update_preferred_x();
	}
}

static void cmd_paste(const char *pf, char **args)
{
	paste();
}

static void cmd_pgdown(const char *pf, char **args)
{
	move_down(window->h - 1 - get_scroll_margin() * 2);
}

static void cmd_pgup(const char *pf, char **args)
{
	move_up(window->h - 1 - get_scroll_margin() * 2);
}

static void cmd_prev(const char *pf, char **args)
{
	prev_buffer();
}

static void cmd_quit(const char *pf, char **args)
{
	struct window *w;
	struct view *v;

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

static void cmd_redo(const char *pf, char **args)
{
	int change_id = 0;

	if (args[0]) {
		change_id = atoi(args[0]);
		if (change_id <= 0) {
			error_msg("Invalid change id: %s", args[0]);
			return;
		}
	}
	if (redo(change_id))
		unselect();
}

static void cmd_repeat(const char *pf, char **args)
{
	const struct command *cmd;
	int count;

	count = atoi(args[0]);
	cmd = find_command(commands, args[1]);
	if (!cmd) {
		error_msg("No such command: %s", args[1]);
		return;
	}

	args += 2;
	pf = parse_args(args, cmd->flags, cmd->min_args, cmd->max_args);
	if (pf) {
		while (count-- > 0)
			cmd->cmd(pf, args);
	}
}

static void cmd_replace(const char *pf, char **args)
{
	unsigned int flags = 0;
	int i;

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

static void cmd_right(const char *pf, char **args)
{
	move_cursor_right();
}

static void cmd_run(const char *pf, char **args)
{
	const char *compiler = NULL;
	struct compiler_format *cf = NULL;
	unsigned int cbits = SPAWN_PIPE_STDOUT | SPAWN_IGNORE_DUPLICATES |
			     SPAWN_IGNORE_REDUNDANT;
	unsigned int flags = 0;
	int jump_to_error = 0;
	int quoted = 0;

	while (*pf) {
		switch (*pf) {
		case '1':
			flags |= SPAWN_PIPE_STDOUT;
			break;
		case 'c':
			quoted = 1;
			break;
		case 'd':
			flags |= SPAWN_IGNORE_DUPLICATES;
			break;
		case 'i':
			flags |= SPAWN_IGNORE_REDUNDANT;
			break;
		case 'j':
			jump_to_error = 1;
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

	if (flags & cbits && !(flags & SPAWN_PIPE_STDOUT))
		flags |= SPAWN_PIPE_STDERR;

	if (flags & (SPAWN_PIPE_STDOUT | SPAWN_PIPE_STDERR) && !compiler) {
		error_msg("Error parser must be specified when collecting error messages.");
		return;
	}
	if (compiler) {
		cf = find_compiler_format(compiler);
		if (!cf) {
			error_msg("No such error parser %s", compiler);
			return;
		}
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
		spawn((char **)array.ptrs, flags, cf);
		ptr_array_free(&array);
	} else {
		spawn(args, flags, cf);
	}

	if (jump_to_error && cerr.count) {
		cerr.pos = 0;
		show_compile_error();
	}
}

static int stat_changed(const struct stat *a, const struct stat *b)
{
	/* don't compare st_mode because we allow chmod 755 etc. */
	return a->st_mtime != b->st_mtime ||
		a->st_dev != b->st_dev ||
		a->st_ino != b->st_ino;
}

static void cmd_save(const char *pf, char **args)
{
	char *absolute = buffer->abs_filename;
	int force = 0;
	enum newline_sequence newline = buffer->newline;
	mode_t old_mode = buffer->st.st_mode;
	struct stat st;
	int new_locked = 0;

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

		free(buffer->abs_filename);
		buffer->abs_filename = absolute;
		update_short_filename(buffer);

		// filename change is not detected (only buffer_modified() change)
		update_flags |= UPDATE_TAB_BAR;
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

static void cmd_scroll_down(const char *pf, char **args)
{
	view->vy++;
	if (view->cy < view->vy)
		move_down(1);
}

static void cmd_scroll_pgdown(const char *pf, char **args)
{
	int max = buffer->nl - window->h + 1;

	if (view->vy < max && max > 0) {
		int count = window->h - 1;

		if (view->vy + count > max)
			count = max - view->vy;
		view->vy += count;
		move_down(count);
	}
}

static void cmd_scroll_pgup(const char *pf, char **args)
{
	if (view->vy > 0) {
		int count = window->h - 1;

		if (count > view->vy)
			count = view->vy;
		view->vy -= count;
		move_up(count);
	}
}

static void cmd_scroll_up(const char *pf, char **args)
{
	if (view->vy)
		view->vy--;
	if (view->vy + window->h <= view->cy)
		move_up(1);
}

static void cmd_search(const char *pf, char **args)
{
	int history = 1;
	int cmd = 0;
	enum search_direction dir = SEARCH_FWD;
	char *word = NULL;
	char *pattern = args[0];

	while (*pf) {
		switch (*pf) {
		case 'H':
			history = 0;
			break;
		case 'n':
		case 'p':
			cmd = *pf;
			break;
		case 'r':
			dir = SEARCH_BWD;
			break;
		case 'w':
			if (pattern) {
				error_msg("Flag -w can't be used with search pattern.");
				return;
			}
			word = get_word_under_cursor();
			if (!word) {
				// error message would not be very useful here
				return;
			}
			break;
		}
		pf++;
	}

	if (word) {
		pattern = xnew(char, strlen(word) + 5);
		sprintf(pattern, "\\<%s\\>", word);
		free(word);
	}

	if (pattern) {
		search_init(dir);
		search(pattern);
		if (history)
			history_add(&search_history, pattern);

		if (pattern != args[0])
			free(pattern);
	} else if (cmd == 'n') {
		search_next();
	} else if (cmd == 'p') {
		search_prev();
	} else {
		input_mode = INPUT_SEARCH;
		search_init(dir);
		update_flags |= UPDATE_COMMAND_LINE;
	}
}

static void cmd_select(const char *pf, char **args)
{
	enum selection sel = SELECT_CHARS;

	if (*pf)
		sel = SELECT_LINES;

	if (selecting()) {
		if (view->selection == sel) {
			unselect();
			return;
		}
		view->selection = sel;
		update_flags |= UPDATE_FULL;
		return;
	}

	view->sel_so = block_iter_get_offset(&view->cursor);
	view->sel_eo = UINT_MAX;
	view->selection = sel;
}

static void cmd_set(const char *pf, char **args)
{
	unsigned int flags = 0;

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

static void cmd_shift(const char *pf, char **args)
{
	int count = 1;

	if (args[0])
		count = atoi(args[0]);
	if (!count) {
		error_msg("Count must be non-zero.");
		return;
	}
	shift_lines(count);
}

static void cmd_suspend(const char *pf, char **args)
{
	ui_end();
	kill(0, SIGSTOP);
}

static void goto_tag(int pos, int save_location)
{
	if (current_tags.count > 1)
		info_msg("[%d/%d]", pos + 1, current_tags.count);
	move_to_tag(current_tags.ptrs[pos], save_location);
}

static void cmd_tag(const char *pf, char **args)
{
	const char *name = args[0];
	static int pos;
	char dir = 0;
	int pop = 0;

	while (*pf) {
		switch (*pf) {
		case 'n':
		case 'p':
			dir = *pf;
			break;
		case 'r':
			pop = 1;
			break;
		}
		pf++;
	}

	if (dir && name) {
		error_msg("Tag and direction (-n/-p) are mutually exclusive.");
		return;
	}
	if (pop) {
		tag_pop();
		return;
	}

	if (!dir) {
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

static void cmd_toggle(const char *pf, char **args)
{
	unsigned int flags = 0;
	int verbose = 0;

	while (*pf) {
		switch (*pf) {
		case 'g':
			flags |= OPT_GLOBAL;
			break;
		case 'l':
			flags |= OPT_LOCAL;
			break;
		case 'v':
			verbose = 1;
			break;
		}
		pf++;
	}
	toggle_option(args[0], flags, verbose);
}

static void cmd_undo(const char *pf, char **args)
{
	if (undo())
		unselect();
}

static void cmd_unselect(const char *pf, char **args)
{
	unselect();
}

static void cmd_up(const char *pf, char **args)
{
	move_up(1);
}

static void cmd_view(const char *pf, char **args)
{
	struct list_head *node;
	int idx;

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

static void cmd_word_bwd(const char *pf, char **args)
{
	int skip_non_word = *pf == 's';
	word_bwd(&view->cursor, skip_non_word);
	update_preferred_x();
}

static void cmd_word_fwd(const char *pf, char **args)
{
	int skip_non_word = *pf == 's';
	word_fwd(&view->cursor, skip_non_word);
	update_preferred_x();
}

const struct command commands[] = {
	{ "alias",		"",	2,  2, cmd_alias },
	{ "bind",		"",	2,  2, cmd_bind },
	{ "bof",		"",	0,  0, cmd_bof },
	{ "bol",		"",	0,  0, cmd_bol },
	{ "case",		"lmu",	0,  0, cmd_case },
	{ "cd",			"",	1,  1, cmd_cd },
	{ "center-view",	"",	0,  0, cmd_center_view },
	{ "clear",		"",	0,  0, cmd_clear },
	{ "close",		"f",	0,  0, cmd_close },
	{ "command",		"",	0,  1, cmd_command },
	{ "copy",		"",	0,  0, cmd_copy },
	{ "cut",		"",	0,  0, cmd_cut },
	{ "delete",		"",	0,  0, cmd_delete },
	{ "delete-eol",		"",	0,  0, cmd_delete_eol },
	{ "delete-word",	"s",	0,  0, cmd_delete_word },
	{ "down",		"",	0,  0, cmd_down },
	{ "eof",		"",	0,  0, cmd_eof },
	{ "eol",		"",	0,  0, cmd_eol },
	{ "erase",		"",	0,  0, cmd_erase },
	{ "erase-bol",		"",	0,  0, cmd_erase_bol },
	{ "erase-word",		"s",	0,  0, cmd_erase_word },
	{ "error",		"np",	0,  1, cmd_error },
	{ "errorfmt",		"ir",	2, -1, cmd_errorfmt },
	{ "filter",		"-",	1, -1, cmd_filter },
	{ "format-paragraph",	"",	0,  1, cmd_format_paragraph },
	{ "ft",			"-cfi",	2, -1, cmd_ft },
	{ "hi",			"",	1, -1, cmd_hi },
	{ "include",		"",	1,  1, cmd_include },
	{ "insert",		"km",	1,  1, cmd_insert },
	{ "insert-special",	"",	0,  0, cmd_insert_special },
	{ "join",		"",	0,  0, cmd_join },
	{ "left",		"",	0,  0, cmd_left },
	{ "line",		"",	1,  1, cmd_line },
	{ "load-syntax",	"",	1,  1, cmd_load_syntax },
	{ "move-tab",		"",	1,  1, cmd_move_tab },
	{ "new-line",		"",	0,  0, cmd_new_line },
	{ "next",		"",	0,  0, cmd_next },
	{ "open",		"",	0, -1, cmd_open },
	{ "option",		"-r",	0, -1, cmd_option },
	{ "pass-through",	"-ms",	1, -1, cmd_pass_through },
	{ "paste",		"",	0,  0, cmd_paste },
	{ "pgdown",		"",	0,  0, cmd_pgdown },
	{ "pgup",		"",	0,  0, cmd_pgup },
	{ "prev",		"",	0,  0, cmd_prev },
	{ "quit",		"f",	0,  0, cmd_quit },
	{ "redo",		"",	0,  1, cmd_redo },
	{ "repeat",		"",	2, -1, cmd_repeat },
	{ "replace",		"bcgi",	2,  2, cmd_replace },
	{ "right",		"",	0,  0, cmd_right },
	{ "run",		"-1cdf=ijps",	1, -1, cmd_run },
	{ "save",		"dfu",	0,  1, cmd_save },
	{ "scroll-down",	"",	0,  0, cmd_scroll_down },
	{ "scroll-pgdown",	"",	0,  0, cmd_scroll_pgdown },
	{ "scroll-pgup",	"",	0,  0, cmd_scroll_pgup },
	{ "scroll-up",		"",	0,  0, cmd_scroll_up },
	{ "search",		"Hnprw",0,  1, cmd_search },
	{ "select",		"l",	0,  0, cmd_select },
	{ "set",		"gl",	1,  2, cmd_set },
	{ "shift",		"",	0,  1, cmd_shift },
	{ "suspend",		"",	0,  0, cmd_suspend },
	{ "tag",		"npr",	0,  1, cmd_tag },
	{ "toggle",		"glv",	1,  1, cmd_toggle },
	{ "undo",		"",	0,  0, cmd_undo },
	{ "unselect",		"",	0,  0, cmd_unselect },
	{ "up",			"",	0,  0, cmd_up },
	{ "view",		"",	1,  1, cmd_view },
	{ "word-bwd",		"s",	0,  0, cmd_word_bwd },
	{ "word-fwd",		"s",	0,  0, cmd_word_fwd },
	{ NULL,			NULL,	0,  0, NULL }
};
