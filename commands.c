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

static void cmd_cancel(const char *pf, char **args)
{
	select_end();
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
	char buf[PATH_MAX];
	struct window *w;
	struct view *v;

	if (chdir(args[0])) {
		error_msg("cd: %s", strerror(errno));
		return;
	}
	if (!getcwd(buf, sizeof(buf))) {
		error_msg("Can't get current workind directory: %s", strerror(errno));
		return;
	}

	list_for_each_entry(w, &windows, node) {
		list_for_each_entry(v, &w->views, node)
			update_short_filename(v->buffer, buf);
	}

	update_flags |= UPDATE_TAB_BAR | UPDATE_STATUS_LINE;
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
	update_flags |= UPDATE_STATUS_LINE;

	if (args[0])
		cmdline_set_text(args[0]);
}

static void cmd_copy(const char *pf, char **args)
{
	struct block_iter save = view->cursor;

	if (selecting()) {
		copy(prepare_selection(), view->selection == SELECT_LINES);
		select_end();
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
		select_end();
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
	struct block_iter bi = view->cursor;
	delete(word_fwd(&bi), 0);
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
	delete(word_bwd(&view->cursor), 1);
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

static void cmd_ft_content(const char *pf, char **args)
{
	add_ft_content(args[0], args[1]);
}

static void cmd_ft_extension(const char *pf, char **args)
{
	add_ft_extensions(args[0], args + 1);
}

static void cmd_ft_match(const char *pf, char **args)
{
	add_ft_match(args[0], args[1]);
}

static const struct command ft_commands[] = {
	{ "content",	"",	2,  2, cmd_ft_content },
	{ "extension",	"",	2, -1, cmd_ft_extension },
	{ "match",	"",	2,  2, cmd_ft_match },
	{ NULL,		NULL,	0,  0, NULL }
};

static void cmd_ft(const char *pf, char **args)
{
	run_command(ft_commands, args);
}

static void cmd_filter(const char *pf, char **args)
{
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

	if (parse_term_color(&color, args + 1))
		set_highlight_color(args[0], &color);
}

static void cmd_include(const char *pf, char **args)
{
	read_config(args[0], 1);
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
			select_end();
		}

		replace(del_len, str, ins_len);
		if (strchr(pf, 'm'))
			block_iter_skip_bytes(&view->cursor, ins_len);
	}
}

static void cmd_insert_special(const char *pf, char **args)
{
	input_special = INPUT_SPECIAL_UNKNOWN;
	update_flags |= UPDATE_STATUS_LINE;
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
	const char *name = args[0];

	if (!find_syntax(name))
		load_syntax(name);
}

static void cmd_move_tab(const char *pf, char **args)
{
	struct list_head *item;
	char *str, *end;
	long num;

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

static char **copy_string_array(char **src, int count)
{
	char **dst = xnew(char *, count + 1);
	int i;

	for (i = 0; i < count; i++)
		dst[i] = xstrdup(src[i]);
	dst[i] = NULL;
	return dst;
}

static void cmd_option_filename(const char *pf, char **args)
{
	int argc = count_strings(args);

	if (argc % 2 == 0) {
		error_msg("Missing option value");
		return;
	}

	add_file_options(FILE_OPTIONS_FILENAME, xstrdup(args[0]), copy_string_array(args + 1, argc - 1));
}

static void cmd_option_filetype(const char *pf, char **args)
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
	{ "filename",	"",	3, -1, cmd_option_filename },
	{ "filetype",	"",	3, -1, cmd_option_filetype },
	{ NULL,		NULL,	0,  0, NULL }
};

static void cmd_option(const char *pf, char **args)
{
	run_command(option_commands, args);
}

static void cmd_pass_through(const char *pf, char **args)
{
	unsigned int del_len = 0;
	int strip_nl;

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

static void cmd_pop(const char *pf, char **args)
{
	pop_location();
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
		select_end();
}

static void cmd_repeat(const char *pf, char **args)
{
	const struct command *cmd;
	int count;

	count = atoi(args[0]);
	cmd = find_command(args[1]);
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
	unsigned int flags = 0;
	int quoted = 0;

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

static void cmd_scroll_pgdown(const char *pf, char **args)
{
	int max = buffer->nl - window->h + 1;

	if (view->vy < max && max > 0) {
		int count = window->h - 1;

		if (view->vy + count > max)
			count = max - view->vy;
		view->vy += count;
		move_down(count);
		update_flags |= UPDATE_FULL;
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
		update_flags |= UPDATE_FULL;
	}
}

static void cmd_search(const char *pf, char **args)
{
	int cmd = 0;
	enum search_direction dir = SEARCH_FWD;
	char *word = NULL;
	char *pattern = args[0];

	while (*pf) {
		switch (*pf) {
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
		update_flags |= UPDATE_STATUS_LINE;
	}
}

static void cmd_select(const char *pf, char **args)
{
	int sel = SELECT_CHARS;

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

static const struct command syn_commands[] = {
	{ "addc",	"hi",	3,  3, syn_addc },
	{ "addr",	"i",	2,  2, syn_addr },
	{ "addw",	"i",	2, -1, syn_addw },
	{ "begin",	"",	1,  1, syn_begin },
	{ "connect",	"",	2, -1, syn_connect },
	{ "end",	"",	0,  0, syn_end },
	{ "join",	"",	2, -1, syn_join },
	{ NULL,		NULL,	0,  0, NULL }
};

static void cmd_syn(const char *pf, char **args)
{
	run_command(syn_commands, args);
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

	while (*pf) {
		switch (*pf) {
		case 'n':
		case 'p':
			dir = *pf;
			break;
		}
		pf++;
	}

	if (dir && name) {
		error_msg("Tag and direction (-n/-p) are mutually exclusive.");
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

static void cmd_undo(const char *pf, char **args)
{
	if (undo())
		select_end();
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
	word_bwd(&view->cursor);
	update_preferred_x();
}

static void cmd_word_fwd(const char *pf, char **args)
{
	word_fwd(&view->cursor);
	update_preferred_x();
}

const struct command commands[] = {
	{ "alias",		"",	2,  2, cmd_alias },
	{ "bind",		"",	2,  2, cmd_bind },
	{ "bof",		"",	0,  0, cmd_bof },
	{ "bol",		"",	0,  0, cmd_bol },
	{ "cancel",		"",	0,  0, cmd_cancel },
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
	{ "delete-word",	"",	0,  0, cmd_delete_word },
	{ "down",		"",	0,  0, cmd_down },
	{ "eof",		"",	0,  0, cmd_eof },
	{ "eol",		"",	0,  0, cmd_eol },
	{ "erase",		"",	0,  0, cmd_erase },
	{ "erase-bol",		"",	0,  0, cmd_erase_bol },
	{ "erase-word",		"",	0,  0, cmd_erase_word },
	{ "error",		"np",	0,  1, cmd_error },
	{ "errorfmt",		"ir",	2, -1, cmd_errorfmt },
	{ "filter",		"-",	1, -1, cmd_filter },
	{ "format-paragraph",	"",	0,  1, cmd_format_paragraph },
	{ "ft",			"-",	0, -1, cmd_ft },
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
	{ "option",		"-",	0, -1, cmd_option },
	{ "pass-through",	"-s",	1, -1, cmd_pass_through },
	{ "paste",		"",	0,  0, cmd_paste },
	{ "pgdown",		"",	0,  0, cmd_pgdown },
	{ "pgup",		"",	0,  0, cmd_pgup },
	{ "pop",		"",	0,  0, cmd_pop },
	{ "prev",		"",	0,  0, cmd_prev },
	{ "quit",		"f",	0,  0, cmd_quit },
	{ "redo",		"",	0,  1, cmd_redo },
	{ "repeat",		"",	2, -1, cmd_repeat },
	{ "replace",		"bcgi",	2,  2, cmd_replace },
	{ "right",		"",	0,  0, cmd_right },
	{ "run",		"-1cdef=ijps",	1, -1, cmd_run },
	{ "save",		"dfu",	0,  1, cmd_save },
	{ "scroll-pgdown",	"",	0,  0, cmd_scroll_pgdown },
	{ "scroll-pgup",	"",	0,  0, cmd_scroll_pgup },
	{ "search",		"nprw",	0,  1, cmd_search },
	{ "select",		"l",	0,  0, cmd_select },
	{ "set",		"gl",	1,  2, cmd_set },
	{ "shift",		"",	0,  1, cmd_shift },
	{ "syn",		"-",	0, -1, cmd_syn },
	{ "tag",		"np",	0,  1, cmd_tag },
	{ "toggle",		"gl",	1,  1, cmd_toggle },
	{ "undo",		"",	0,  0, cmd_undo },
	{ "up",			"",	0,  0, cmd_up },
	{ "view",		"",	1,  1, cmd_view },
	{ "word-bwd",		"",	0,  0, cmd_word_bwd },
	{ "word-fwd",		"",	0,  0, cmd_word_fwd },
	{ NULL,			NULL,	0,  0, NULL }
};
