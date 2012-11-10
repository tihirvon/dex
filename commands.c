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
#include "filetype.h"
#include "color.h"
#include "state.h"
#include "syntax.h"
#include "lock.h"
#include "ptr-array.h"
#include "bind.h"
#include "alias.h"
#include "tag.h"
#include "config.h"
#include "command.h"
#include "parse-args.h"
#include "file-option.h"
#include "msg.h"
#include "frame.h"
#include "load-save.h"
#include "selection.h"
#include "encoding.h"
#include "path.h"
#include "error.h"
#include "input-special.h"
#include "git-open.h"

static void cmd_alias(const char *pf, char **args)
{
	add_alias(args[0], args[1]);
}

static void cmd_bind(const char *pf, char **args)
{
	if (args[1])
		add_binding(args[0], args[1]);
	else
		remove_binding(args[0]);
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

	while (*pf) {
		switch (*pf) {
		case 'l':
		case 'u':
			mode = *pf;
			break;
		}
		pf++;
	}
	change_case(mode);
}

static void cmd_cd(const char *pf, char **args)
{
	char cwd[8192];
	char *cwdp = NULL;
	bool got_cwd = !!getcwd(cwd, sizeof(cwd));
	int i, j;

	if (chdir(args[0])) {
		error_msg("cd: %s", strerror(errno));
		return;
	}

	if (got_cwd)
		setenv("OLDPWD", cwd, 1);
	got_cwd = !!getcwd(cwd, sizeof(cwd));
	if (got_cwd) {
		setenv("PWD", cwd, 1);
		cwdp = cwd;
	}

	for (i = 0; i < windows.count; i++) {
		for (j = 0; j < WINDOW(i)->views.count; j++) {
			update_short_filename_cwd(VIEW(i, j)->buffer, cwdp);
		}
	}

	// need to update all tabbars
	mark_everything_changed();
}

static void cmd_center_view(const char *pf, char **args)
{
	view->force_center = true;
}

static void cmd_clear(const char *pf, char **args)
{
	clear_lines();
}

static void cmd_close(const char *pf, char **args)
{
	if (buffer_modified(buffer) && buffer->views.count == 1 && !*pf) {
		error_msg("The buffer is modified. Save or run 'close -f' to close without saving.");
		return;
	}
	close_current_view();
}

static void cmd_command(const char *pf, char **args)
{
	input_mode = INPUT_COMMAND;
	if (args[0])
		cmdline_set_text(&cmdline, args[0]);
}

static void cmd_compile(const char *pf, char **args)
{
	struct compiler *c;
	unsigned int flags = 0;
	const char *name;

	while (*pf) {
		switch (*pf) {
		case '1':
			flags |= SPAWN_READ_STDOUT;
			break;
		case 'p':
			flags |= SPAWN_PROMPT;
			break;
		case 's':
			flags |= SPAWN_QUIET;
			break;
		}
		pf++;
	}

	name= *args++;
	c = find_compiler(name);
	if (!c) {
		error_msg("No such error parser %s", name);
		return;
	}
	clear_messages();
	spawn_compiler(args, flags, c);
	if (message_count())
		current_message(1);
}

static void cmd_copy(const char *pf, char **args)
{
	struct block_iter save = view->cursor;

	if (selecting()) {
		copy(prepare_selection(), view->selection == SELECT_LINES);
		unselect();
	} else {
		struct block_iter tmp;
		block_iter_bol(&view->cursor);
		tmp = view->cursor;
		copy(block_iter_eat_line(&tmp), 1);
	}
	view->cursor = save;
}

static void cmd_cut(const char *pf, char **args)
{
	int x = get_preferred_x();

	if (selecting()) {
		cut(prepare_selection(), view->selection == SELECT_LINES);
		if (view->selection == SELECT_LINES) {
			move_to_preferred_x(x);
		}
		unselect();
	} else {
		struct block_iter tmp;
		block_iter_bol(&view->cursor);
		tmp = view->cursor;
		cut(block_iter_eat_line(&tmp), 1);
		move_to_preferred_x(x);
	}
}

static void cmd_delete(const char *pf, char **args)
{
	delete_ch();
}

static void cmd_delete_eol(const char *pf, char **args)
{
	struct block_iter bi = view->cursor;
	buffer_delete_bytes(block_iter_eol(&bi));
}

static void cmd_delete_word(const char *pf, char **args)
{
	bool skip_non_word = *pf == 's';
	struct block_iter bi = view->cursor;

	buffer_delete_bytes(word_fwd(&bi, skip_non_word));
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
	buffer_erase_bytes(block_iter_bol(&view->cursor));
}

static void cmd_erase_word(const char *pf, char **args)
{
	bool skip_non_word = *pf == 's';
	buffer_erase_bytes(word_bwd(&view->cursor, skip_non_word));
}

static void cmd_errorfmt(const char *pf, char **args)
{
	bool ignore = false;

	while (*pf) {
		switch (*pf) {
		case 'i':
			ignore = true;
			break;
		}
		pf++;
	}
	add_error_fmt(args[0], ignore, args[1], args + 2);
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
	buffer_replace_bytes(data.in_len, data.out, data.out_len);
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

static void cmd_git_open(const char *pf, char **args)
{
	input_mode = INPUT_GIT_OPEN;
	git_open_reload();
}

static void cmd_hi(const char *pf, char **args)
{
	struct term_color color;

	if (args[0] == NULL) {
		exec_builtin_rc(reset_colors_rc);
		remove_extra_colors();
	} else if (parse_term_color(&color, args + 1)) {
		set_highlight_color(args[0], &color);
	}

	// Don't call update_all_syntax_colors() needlessly.
	// It is called right after config has been loaded.
	if (editor_status != EDITOR_INITIALIZING) {
		update_all_syntax_colors();
		mark_everything_changed();
	}
}

static void cmd_include(const char *pf, char **args)
{
	read_config(commands, args[0], true);
}

static void cmd_insert(const char *pf, char **args)
{
	const char *str = args[0];

	if (strchr(pf, 'k')) {
		int i;
		for (i = 0; str[i]; i++)
			insert_ch(str[i]);
	} else {
		long del_len = 0;
		long ins_len = strlen(str);

		if (selecting()) {
			del_len = prepare_selection();
			unselect();
		}

		buffer_replace_bytes(del_len, str, ins_len);
		if (strchr(pf, 'm'))
			block_iter_skip_bytes(&view->cursor, ins_len);
	}
}

static void cmd_insert_special(const char *pf, char **args)
{
	special_input_activate();
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
	int x = get_preferred_x();
	int line;

	line = atoi(args[0]);
	if (line > 0) {
		move_to_line(line);
		move_to_preferred_x(x);
	}
}

static void cmd_load_syntax(const char *pf, char **args)
{
	const char *slash = strrchr(args[0], '/');
	const char *filename = slash ? args[0] : NULL;
	const char *filetype = slash ? slash + 1 : args[0];
	int err;

	if (filename) {
		if (find_syntax(filetype)) {
			error_msg("Syntax for filetype %s already loaded", filetype);
		} else {
			load_syntax_file(filename, true, &err);
		}
	} else {
		if (!find_syntax(filetype))
			load_syntax_by_filetype(filetype);
	}
}

static void cmd_move_tab(const char *pf, char **args)
{
	int j, i = view_idx();
	char *str = args[0];

	if (streq(str, "left")) {
		j = new_view_idx(i - 1);
	} else if (streq(str, "right")) {
		j = new_view_idx(i + 1);
	} else {
		long num;
		if (!str_to_long(str, &num) || num < 1) {
			error_msg("Invalid tab position %s", str);
			return;
		}
		j = num - 1;
		if (j >= window->views.count)
			j = window->views.count - 1;
	}

	ptr_array_insert(&window->views, ptr_array_remove(&window->views, i), j);
	window->update_tabbar = true;
}

static void cmd_msg(const char *pf, char **args)
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

	if (dir == 'n') {
		next_message();
	} else if (dir == 'p') {
		prev_message();
	} else {
		current_message(0);
	}
}

static void cmd_new_line(const char *pf, char **args)
{
	new_line();
}

static void cmd_next(const char *pf, char **args)
{
	set_view(window->views.ptrs[new_view_idx(view_idx() + 1)]);
}

static void cmd_open(const char *pf, char **args)
{
	const char *enc = NULL;
	char *encoding = NULL;

	while (*pf) {
		switch (*pf) {
		case 'e':
			enc = *args++;
			break;
		}
		pf++;
	}

	if (enc) {
		encoding = normalize_encoding(enc);
		if (encoding == NULL) {
			error_msg("Unsupported encoding %s", enc);
			return;
		}
	}

	if (!args[0]) {
		open_new_file();
		if (encoding) {
			free(buffer->encoding);
			buffer->encoding = encoding;
		}
		return;
	}
	if (!args[1]) {
		// previous view is remembered when opening single file
		open_file(args[0], encoding);
	} else {
		// it makes no sense to remember previous view when opening multiple files
		open_files(args, encoding);
	}
	free(encoding);
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
		add_file_options(FILE_OPTIONS_FILETYPE, xstrslice(list, 0, len), strs);
		list = comma + 1;
	} while (comma);
}

static void cmd_pass_through(const char *pf, char **args)
{
	struct filter_data data;
	long del_len = 0;
	bool strip_nl = false;
	bool move = false;

	while (*pf) {
		switch (*pf++) {
		case 'm':
			move = true;
			break;
		case 's':
			strip_nl = true;
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

	buffer_replace_bytes(del_len, data.out, data.out_len);
	free(data.out);

	if (move) {
		block_iter_skip_bytes(&view->cursor, data.out_len);
	}
}

static void cmd_paste(const char *pf, char **args)
{
	paste();
}

static void cmd_pgdown(const char *pf, char **args)
{
	move_down(window->edit_h - 1 - get_scroll_margin() * 2);
}

static void cmd_pgup(const char *pf, char **args)
{
	move_up(window->edit_h - 1 - get_scroll_margin() * 2);
}

static void cmd_prev(const char *pf, char **args)
{
	set_view(window->views.ptrs[new_view_idx(view_idx() - 1)]);
}

static void cmd_quit(const char *pf, char **args)
{
	int i, j;

	if (pf[0]) {
		editor_status = EDITOR_EXITING;
		return;
	}
	for (i = 0; i < windows.count; i++) {
		for (j = 0; j < WINDOW(i)->views.count; j++) {
			struct view *v = VIEW(i, j);
			if (buffer_modified(v->buffer)) {
				set_view(v);
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
	if (redo(change_id)) {
		unselect();
	}
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
	int fd[3] = { 0, 1, 2 };
	bool prompt = false;

	while (*pf) {
		switch (*pf) {
		case 'p':
			prompt = true;
			break;
		case 's':
			fd[0] = -1;
			fd[1] = -1;
			fd[2] = -1;
			break;
		}
		pf++;
	}
	spawn(args, fd, prompt);
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
	char *encoding = buffer->encoding;
	const char *enc = NULL;
	bool force = false;
	enum newline_sequence newline = buffer->newline;
	mode_t old_mode = buffer->st.st_mode;
	struct stat st;
	bool new_locked = false;

	while (*pf) {
		switch (*pf) {
		case 'd':
			newline = NEWLINE_DOS;
			break;
		case 'e':
			enc = *args++;
			break;
		case 'f':
			force = true;
			break;
		case 'u':
			newline = NEWLINE_UNIX;
			break;
		}
		pf++;
	}

	if (enc) {
		encoding = normalize_encoding(enc);
		if (encoding == NULL) {
			error_msg("Unsupported encoding %s", enc);
			return;
		}
	}

	if (args[0]) {
		char *tmp = path_absolute(args[0]);
		if (!tmp) {
			error_msg("Failed to make absolute path: %s", strerror(errno));
			return;
		}
		if (absolute && streq(tmp, absolute)) {
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
						buffer->locked = true;
					}
				}
			} else {
				if (lock_file(absolute)) {
					if (!force) {
						error_msg("Can't lock file %s", absolute);
						goto error;
					}
				} else {
					new_locked = true;
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
						buffer->locked = true;
					}
				}
			} else {
				if (lock_file(absolute)) {
					if (!force) {
						error_msg("Can't lock file %s", absolute);
						goto error;
					}
				} else {
					new_locked = true;
				}
			}
		}
		if (absolute != buffer->abs_filename && !force) {
			error_msg("Use -f to overwrite %s.", absolute);
			goto error;
		}

		/* allow chmod 755 etc. */
		buffer->st.st_mode = st.st_mode;
	}
	if (save_buffer(absolute, encoding, newline))
		goto error;

	buffer->saved_change = buffer->cur_change;
	buffer->ro = false;
	buffer->newline = newline;
	if (encoding != buffer->encoding) {
		free(buffer->encoding);
		buffer->encoding = encoding;
	}

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
		mark_buffer_tabbars_changed();
	}
	if (!old_mode && streq(buffer->options.filetype, "none")) {
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
	if (encoding != buffer->encoding)
		free(encoding);
}

static void cmd_scroll_down(const char *pf, char **args)
{
	view->vy++;
	if (view->cy < view->vy)
		move_down(1);
}

static void cmd_scroll_pgdown(const char *pf, char **args)
{
	int max = buffer->nl - window->edit_h + 1;

	if (view->vy < max && max > 0) {
		int count = window->edit_h - 1;

		if (view->vy + count > max)
			count = max - view->vy;
		view->vy += count;
		move_down(count);
	}
}

static void cmd_scroll_pgup(const char *pf, char **args)
{
	if (view->vy > 0) {
		int count = window->edit_h - 1;

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
	if (view->vy + window->edit_h <= view->cy)
		move_up(1);
}

static void cmd_search(const char *pf, char **args)
{
	bool history = true;
	char cmd = 0;
	bool w = false;
	enum search_direction dir = SEARCH_FWD;
	char *pattern = args[0];

	while (*pf) {
		switch (*pf) {
		case 'H':
			history = false;
			break;
		case 'n':
		case 'p':
			cmd = *pf;
			break;
		case 'r':
			dir = SEARCH_BWD;
			break;
		case 'w':
			w = true;
			if (pattern) {
				error_msg("Flag -w can't be used with search pattern.");
				return;
			}
			break;
		}
		pf++;
	}

	if (w) {
		char *word = get_word_under_cursor();
		if (word == NULL) {
			// error message would not be very useful here
			return;
		}
		pattern = xnew(char, strlen(word) + 5);
		sprintf(pattern, "\\<%s\\>", word);
		free(word);
	}

	if (pattern) {
		search_set_direction(dir);
		search_set_regexp(pattern);
		if (w) {
			search_next_word();
		} else {
			search_next();
		}
		if (history)
			history_add(&search_history, pattern, search_history_size);

		if (pattern != args[0])
			free(pattern);
	} else if (cmd == 'n') {
		search_next();
	} else if (cmd == 'p') {
		search_prev();
	} else {
		input_mode = INPUT_SEARCH;
		search_set_direction(dir);
	}
}

static void cmd_select(const char *pf, char **args)
{
	enum selection sel = SELECT_CHARS;
	bool block = false;

	while (*pf) {
		switch (*pf) {
		case 'b':
			block = true;
			break;
		case 'l':
			block = false;
			sel = SELECT_LINES;
			break;
		}
		pf++;
	}

	if (block) {
		select_block();
		return;
	}

	if (selecting()) {
		if (view->selection == sel) {
			unselect();
			return;
		}
		view->selection = sel;
		mark_all_lines_changed();
		return;
	}

	view->sel_so = block_iter_get_offset(&view->cursor);
	view->sel_eo = UINT_MAX;
	view->selection = sel;

	// need to mark current line changed because cursor might
	// move up or down before screen is updated
	update_cursor_y();
	lines_changed(view->cy, view->cy);
}

static void cmd_set(const char *pf, char **args)
{
	bool global = false;
	bool local = false;
	int i, count = count_strings(args);

	while (*pf) {
		switch (*pf) {
		case 'g':
			global = true;
			break;
		case 'l':
			local = true;
			break;
		}
		pf++;
	}

	// you can set only global values in config file
	if (buffer == NULL) {
		if (local) {
			error_msg("Flag -l makes no sense in config file.");
			return;
		}
		global = true;
	}

	if (count == 1) {
		// set boolean to true
		set_option(args[0], NULL, local, global);
		return;
	}
	if (count % 2) {
		error_msg("One or even number of arguments expected.");
		return;
	}
	for (i = 0; args[i]; i += 2)
		set_option(args[i], args[i + 1], local, global);
}

static void cmd_setenv(const char *pf, char **args)
{
	if (setenv(args[0], args[1], 1) < 0) {
		switch (errno) {
		case EINVAL:
			error_msg("Invalid environment variable name '%s'", args[0]);
			break;
		default:
			error_msg("%s", strerror(errno));
		}
	}
}

static void cmd_shift(const char *pf, char **args)
{
	int count = atoi(args[0]);

	if (!count) {
		error_msg("Count must be non-zero.");
		return;
	}
	shift_lines(count);
}

static void cmd_suspend(const char *pf, char **args)
{
	suspend();
}

static void cmd_tag(const char *pf, char **args)
{
	PTR_ARRAY(tags);
	const char *name = args[0];
	char *word = NULL;
	bool pop = false;

	while (*pf) {
		switch (*pf) {
		case 'r':
			pop = true;
			break;
		}
		pf++;
	}

	if (pop) {
		pop_location();
		return;
	}

	if (!name) {
		word = get_word_under_cursor();
		if (!word)
			return;
		name = word;
	}

	clear_messages();

	if (!find_tags(name, &tags)) {
		error_msg("No tag file.");
	} else if (!tags.count) {
		error_msg("Tag %s not found.", name);
	} else {
		int i;
		for (i = 0; i < tags.count; i++) {
			struct tag *t = tags.ptrs[i];
			struct message *m;
			char buf[512];

			snprintf(buf, sizeof(buf), "Tag %s", name);
			m = new_message(buf);
			m->file = t->filename;
			t->filename = NULL;
			if (t->pattern) {
				m->u.pattern = t->pattern;
				m->pattern_is_set = true;
				t->pattern = NULL;
			} else {
				m->u.location.line = t->line;
			}
			add_message(m);
		}
		free_tags(&tags);
		current_message(1);
	}
	free(word);
}

static void cmd_toggle(const char *pf, char **args)
{
	bool global = false;
	bool verbose = false;

	while (*pf) {
		switch (*pf) {
		case 'g':
			global = true;
			break;
		case 'v':
			verbose = true;
			break;
		}
		pf++;
	}
	if (args[1])
		toggle_option_values(args[0], global, verbose, args + 1);
	else
		toggle_option(args[0], global, verbose);
}

static void cmd_undo(const char *pf, char **args)
{
	if (undo()) {
		unselect();
	}
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
	int idx;

	if (streq(args[0], "last")) {
		idx = window->views.count - 1;
	} else {
		idx = atoi(args[0]) - 1;
		if (idx < 0) {
			error_msg("View number must be positive.");
			return;
		}
		if (idx > window->views.count - 1)
			idx = window->views.count - 1;
	}
	set_view(window->views.ptrs[idx]);
}

static int activate_modified_buffer(void)
{
	int i;

	for (i = 0; i < window->views.count; i++) {
		struct view *v = window->views.ptrs[i];
		if (buffer_modified(v->buffer) && v->buffer->views.count == 1) {
			// buffer modified and only in this window
			set_view(v);
			return true;
		}
	}
	return false;
}

static void cmd_wclose(const char *pf, char **args)
{
	int i, idx;
	bool force = !!*pf;
	struct window *w;

	if (!force && activate_modified_buffer()) {
		error_msg("Save modified files or run 'wclose -f' to close window without saving.");
		return;
	}
	for (i = 0; i < window->views.count; i++)
		view_delete(window->views.ptrs[i]);
	window->views.count = 0;
	view = NULL;
	buffer = NULL;

	if (windows.count == 1) {
		// don't close last window
		set_view(open_empty_buffer());
		return;
	}

	idx = window_idx();
	w = ptr_array_remove(&windows, idx);
	remove_frame(w->frame);
	free(w->views.ptrs);
	free(w);

	if (idx == windows.count)
		idx = windows.count - 1;
	window = windows.ptrs[idx];
	set_view(window->view);

	mark_everything_changed();
	debug_frames();
}

static void cmd_wflip(const char *pf, char **args)
{
	struct frame *f = window->frame;

	if (f->parent == NULL)
		return;

	f->parent->vertical ^= 1;
	mark_everything_changed();
}

static void cmd_wnext(const char *pf, char **args)
{
	window = windows.ptrs[new_window_idx(window_idx() + 1)];
	set_view(window->view);
	mark_everything_changed();
	debug_frames();
}

static void cmd_word_bwd(const char *pf, char **args)
{
	bool skip_non_word = *pf == 's';
	word_bwd(&view->cursor, skip_non_word);
	reset_preferred_x();
}

static void cmd_word_fwd(const char *pf, char **args)
{
	bool skip_non_word = *pf == 's';
	word_fwd(&view->cursor, skip_non_word);
	reset_preferred_x();
}

static void cmd_wprev(const char *pf, char **args)
{
	window = windows.ptrs[new_window_idx(window_idx() - 1)];
	set_view(window->view);
	mark_everything_changed();
	debug_frames();
}

static void cmd_wresize(const char *pf, char **args)
{
	enum resize_direction dir = RESIZE_DIRECTION_AUTO;
	const char *arg = *args;

	while (*pf) {
		switch (*pf) {
		case 'h':
			dir = RESIZE_DIRECTION_HORIZONTAL;
			break;
		case 'v':
			dir = RESIZE_DIRECTION_VERTICAL;
			break;
		}
		pf++;
	}
	if (window->frame->parent == NULL) {
		// only window
		return;
	}
	if (arg) {
		int n = atoi(arg);

		if (arg[0] == '+' || arg[0] == '-')
			add_to_frame_size(window->frame, dir, n);
		else
			resize_frame(window->frame, dir, n);
	} else {
		equalize_frame_sizes(window->frame->parent);
	}
	mark_everything_changed();
	debug_frames();
}

static void cmd_wsplit(const char *pf, char **args)
{
	bool before = false;
	bool vertical = false;
	bool root = false;
	struct frame *f;
	struct view *save;

	while (*pf) {
		switch (*pf) {
		case 'b':
			// add new window before current window
			before = true;
			break;
		case 'h':
			// split horizontally to get vertical layout
			vertical = true;
			break;
		case 'r':
			// split root frame instead of current window
			root = true;
			break;
		}
		pf++;
	}

	if (root)
		f = split_root(vertical, before);
	else
		f = split_frame(window, vertical, before);

	save = view;
	window = f->window;
	view = NULL;
	buffer = NULL;
	mark_everything_changed();

	if (*args) {
		cmd_open("", args);
	} else {
		struct view *new = window_add_buffer(save->buffer);
		new->cursor = save->cursor;
		set_view(new);
	}

	if (window->views.count == 0) {
		// open failed, remove new window
		ptr_array_remove(&windows, ptr_array_idx(&windows, window));
		remove_frame(window->frame);
		free(window->views.ptrs);
		free(window);

		view = save;
		buffer = view->buffer;
		window = view->window;
	}

	debug_frames();
}

static void cmd_wswap(const char *pf, char **args)
{
	struct frame *tmp, *parent = window->frame->parent;
	int i, j;

	if (parent == NULL)
		return;

	i = ptr_array_idx(&parent->frames, window->frame);
	j = i + 1;
	if (j == parent->frames.count)
		j = 0;

	tmp = parent->frames.ptrs[i];
	parent->frames.ptrs[i] = parent->frames.ptrs[j];
	parent->frames.ptrs[j] = tmp;
	mark_everything_changed();
}

const struct command commands[] = {
	{ "alias",		"",	2,  2, cmd_alias },
	{ "bind",		"",	1,  2, cmd_bind },
	{ "bof",		"",	0,  0, cmd_bof },
	{ "bol",		"",	0,  0, cmd_bol },
	{ "case",		"lu",	0,  0, cmd_case },
	{ "cd",			"",	1,  1, cmd_cd },
	{ "center-view",	"",	0,  0, cmd_center_view },
	{ "clear",		"",	0,  0, cmd_clear },
	{ "close",		"f",	0,  0, cmd_close },
	{ "command",		"",	0,  1, cmd_command },
	{ "compile",		"-1ps",	2, -1, cmd_compile },
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
	{ "errorfmt",		"i",	2,  6, cmd_errorfmt },
	{ "filter",		"-",	1, -1, cmd_filter },
	{ "format-paragraph",	"",	0,  1, cmd_format_paragraph },
	{ "ft",			"-cfi",	2, -1, cmd_ft },
	{ "git-open",		"",	0,  0, cmd_git_open },
	{ "hi",			"-",	0, -1, cmd_hi },
	{ "include",		"",	1,  1, cmd_include },
	{ "insert",		"km",	1,  1, cmd_insert },
	{ "insert-special",	"",	0,  0, cmd_insert_special },
	{ "join",		"",	0,  0, cmd_join },
	{ "left",		"",	0,  0, cmd_left },
	{ "line",		"",	1,  1, cmd_line },
	{ "load-syntax",	"",	1,  1, cmd_load_syntax },
	{ "move-tab",		"",	1,  1, cmd_move_tab },
	{ "msg",		"np",	0,  0, cmd_msg },
	{ "new-line",		"",	0,  0, cmd_new_line },
	{ "next",		"",	0,  0, cmd_next },
	{ "open",		"e=",	0, -1, cmd_open },
	{ "option",		"-r",	3, -1, cmd_option },
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
	{ "run",		"-ps",	1, -1, cmd_run },
	{ "save",		"de=fu",0,  1, cmd_save },
	{ "scroll-down",	"",	0,  0, cmd_scroll_down },
	{ "scroll-pgdown",	"",	0,  0, cmd_scroll_pgdown },
	{ "scroll-pgup",	"",	0,  0, cmd_scroll_pgup },
	{ "scroll-up",		"",	0,  0, cmd_scroll_up },
	{ "search",		"Hnprw",0,  1, cmd_search },
	{ "select",		"bl",	0,  0, cmd_select },
	{ "set",		"gl",	1, -1, cmd_set },
	{ "setenv",		"",	2,  2, cmd_setenv },
	{ "shift",		"",	1,  1, cmd_shift },
	{ "suspend",		"",	0,  0, cmd_suspend },
	{ "tag",		"r",	0,  1, cmd_tag },
	{ "toggle",		"glv",	1, -1, cmd_toggle },
	{ "undo",		"",	0,  0, cmd_undo },
	{ "unselect",		"",	0,  0, cmd_unselect },
	{ "up",			"",	0,  0, cmd_up },
	{ "view",		"",	1,  1, cmd_view },
	{ "wclose",		"f",	0,  0, cmd_wclose },
	{ "wflip",		"",	0,  0, cmd_wflip },
	{ "wnext",		"",	0,  0, cmd_wnext },
	{ "word-bwd",		"s",	0,  0, cmd_word_bwd },
	{ "word-fwd",		"s",	0,  0, cmd_word_fwd },
	{ "wprev",		"",	0,  0, cmd_wprev },
	{ "wresize",		"hv",	0,  1, cmd_wresize },
	{ "wsplit",		"bhr",	0, -1, cmd_wsplit },
	{ "wswap",		"",	0,  0, cmd_wswap },
	{ NULL,			NULL,	0,  0, NULL }
};
