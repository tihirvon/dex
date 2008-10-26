#include "commands.h"
#include "edit.h"
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

#define MAX_KEYS 4

struct binding {
	struct list_head node;
	char *command;
	enum term_key_type types[MAX_KEYS];
	unsigned int keys[MAX_KEYS];
	int nr_keys;
};

int nr_pressed_keys;
const struct command *current_command;

static enum term_key_type pressed_types[MAX_KEYS];
static unsigned int pressed_keys[MAX_KEYS];

static LIST_HEAD(bindings);
static const char *special_names[NR_SKEYS] = {
	"backspace",
	"insert",
	"delete",
	"home",
	"end",
	"pgup",
	"pgdown",
	"left",
	"right",
	"up",
	"down",
	"f1",
	"f2",
	"f3",
	"f4",
	"f5",
	"f6",
	"f7",
	"f8",
	"f9",
	"f10",
	"f11",
	"f12",
};

static void run_command(const struct command *cmds, char **av);

static int parse_key(enum term_key_type *type, unsigned int *key, const char *str)
{
	int i, len = strlen(str);
	char ch;

	if (len == 1) {
		*type = KEY_NORMAL;
		*key = str[0];
		return 1;
	}
	if (!strcasecmp(str, "sp") || !strcasecmp(str, "space")) {
		*type = KEY_NORMAL;
		*key = ' ';
		return 1;
	}
	ch = toupper(str[1]);
	if (str[0] == '^' && ch >= 0x40 && ch < 0x60 && len == 2) {
		*type = KEY_NORMAL;
		*key = ch - 0x40;
		return 1;
	}
	if (toupper(str[0]) == 'M' && str[1] == '-' && parse_key(type, key, str + 2)) {
		*type = KEY_META;
		return 1;
	}
	for (i = 0; i < NR_SKEYS; i++) {
		if (!strcasecmp(str, special_names[i])) {
			*type = KEY_SPECIAL;
			*key = i;
			return 1;
		}
	}
	error_msg("Invalid key %s", str);
	return 0;
}

/*
 * Flags and first "--" are removed.
 * Flag arguments are moved to beginning.
 * Other arguments come right after flag arguments.
 *
 * Returns parsed flags (order is preserved).
 */
const char *parse_args(char ***argsp, const char *flag_desc, int min, int max)
{
	static char flags[16];
	char **args = *argsp;
	int argc;
	int nr_flags = 0;
	int nr_flag_args = 0;
	int i, j;

	for (argc = 0; args[argc]; argc++)
		;

	i = 0;
	while (args[i]) {
		char *arg = args[i];

		if (!strcmp(arg, "--")) {
			/* move the NULL too */
			memmove(args + i, args + i + 1, (argc - i) * sizeof(*args));
			free(arg);
			argc--;
			break;
		}
		if (arg[0] != '-' || !arg[1]) {
			i++;
			continue;
		}

		for (j = 1; arg[j]; j++) {
			char flag = arg[j];
			char *flag_arg;
			char *flagp = strchr(flag_desc, flag);

			if (!flagp || flag == '=') {
				error_msg("Invalid option -%c", flag);
				return NULL;
			}
			flags[nr_flags++] = flag;
			if (nr_flags == ARRAY_COUNT(flags)) {
				error_msg("Too many options given.");
				return NULL;
			}
			if (flagp[1] != '=')
				continue;

			if (j > 1 || arg[j + 1]) {
				error_msg("Flag -%c must be given separately because it requires an argument.", flag);
				return NULL;
			}
			flag_arg = args[i + 1];
			if (!flag_arg) {
				error_msg("Option -%c requires on argument.", flag);
				return NULL;
			}

			/* move flag argument before any other arguments */
			if (i != nr_flag_args) {
				// farg1 arg1  arg2 -f   farg2 arg3
				// farg1 farg2 arg1 arg2 arg3
				int count = i - nr_flag_args;
				memmove(args + nr_flag_args + 1, args + nr_flag_args, count * sizeof(*args));
			}
			args[nr_flag_args++] = flag_arg;
			i++;
		}

		memmove(args + i, args + i + 1, (argc - i) * sizeof(*args));
		free(arg);
		argc--;
	}
	if (argc < min) {
		error_msg("Not enough arguments");
		return NULL;
	}
	if (max >= 0 && argc > max) {
		error_msg("Too many arguments");
		return NULL;
	}
	flags[nr_flags] = 0;
	return flags;
}

static void cmd_erase(char **args)
{
	erase();
}

static void cmd_bind(char **args)
{
	const char *pf = parse_args(&args, "", 2, 2);
	struct binding *b;
	char *keys;
	int count = 0, i = 0;

	if (!pf)
		return;

	b = xnew(struct binding, 1);
	b->command = xstrdup(args[1]);

	keys = args[0];
	while (keys[i]) {
		int start = i;

		if (count >= MAX_KEYS)
			goto error;

		i++;
		while (keys[i] && keys[i] != ' ')
			i++;
		if (keys[i] == ' ')
			keys[i++] = 0;
		if (!parse_key(&b->types[count], &b->keys[count], keys + start))
			goto error;
		count++;
	}
	b->nr_keys = count;
	if (!count) {
		error_msg("Empty key not allowed.");
		goto error;
	}
	list_add_before(&b->node, &bindings);
	return;
error:
	free(b->command);
	free(b);
}

static void cmd_bof(char **args)
{
	move_bof();
}

static void cmd_bol(char **args)
{
	move_bol();
}

static void cmd_cancel(char **args)
{
	select_end();
}

static void cmd_center_cursor(char **args)
{
	const char *pf = parse_args(&args, "", 0, 0);

	if (!pf)
		return;
	center_cursor();
}

static void cmd_clear(char **args)
{
	const char *pf = parse_args(&args, "", 0, 0);

	if (!pf)
		return;
	clear_lines();
}

static void cmd_close(char **args)
{
	const char *pf = parse_args(&args, "f", 0, 0);

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
	unsigned int len;

	undo_merge = UNDO_MERGE_NONE;
	if (view->sel.blk) {
		len = prepare_selection();
	} else {
		len = select_current_line();
	}
	copy(len, view->sel_is_lines);
	select_end();
	move_preferred_x();
}

static void cmd_cut(char **args)
{
	unsigned int len;
	int restore_col;

	undo_merge = UNDO_MERGE_NONE;
	if (view->sel.blk) {
		len = prepare_selection();
		restore_col = view->sel_is_lines;
	} else {
		len = select_current_line();
		restore_col = 1;
	}
	cut(len, view->sel_is_lines);
	select_end();
	if (restore_col)
		move_preferred_x();
}

static void cmd_delete(char **args)
{
	delete_ch();
}

static void cmd_delete_bol(char **args)
{
	undo_merge = UNDO_MERGE_NONE;
	delete(block_iter_bol(&view->cursor), 1);
}

static void cmd_delete_eol(char **args)
{
	struct block_iter bi = view->cursor;
	unsigned int len = count_bytes_eol(&bi);

	undo_merge = UNDO_MERGE_NONE;
	if (len > 1)
		delete(len - 1, 0);
}

static void cmd_down(char **args)
{
	move_down(1);
}

static void cmd_eof(char **args)
{
	move_eof();
}

static void cmd_eol(char **args)
{
	move_eol();
}

static void cmd_erase_word(char **args)
{
	erase_word();
}

static void cmd_error(char **args)
{
	const char *pf = parse_args(&args, "np", 0, 1);
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
	const char *pf = parse_args(&args, "ir", 2, -1);
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

static void cmd_filetype_content(char **args)
{
	if (!parse_args(&args, "", 2, 2))
		return;
	add_ft_content(args[0], args[1]);
}

static void cmd_filetype_extension(char **args)
{
	if (!parse_args(&args, "", 2, -1))
		return;
	add_ft_extensions(args[0], args + 1);
}

static void cmd_filetype_match(char **args)
{
	if (!parse_args(&args, "", 2, 2))
		return;
	add_ft_match(args[0], args[1]);
}

static const struct command filetype_commands[] = {
	{ "content", NULL, cmd_filetype_content },
	{ "extension", "ext", cmd_filetype_extension },
	{ "match", NULL, cmd_filetype_match },
	{ NULL, NULL, NULL }
};

static void cmd_filetype(char **args)
{
	run_command(filetype_commands, args);
}

static char *shell_unescape(const char *str)
{
	GBUF(buf);
	int i = 0;

	while (str[i]) {
		char ch = str[i++];
		if (ch == '\\' && str[i]) {
			ch = str[i++];
			switch (ch) {
			case 'n':
				ch = '\n';
				break;
			case 'r':
				ch = '\r';
				break;
			}
		}
		gbuf_add_ch(&buf, ch);
	}
	return gbuf_steal(&buf);
}

static const char * const color_names[] = {
	"keep", "default",
	"black", "red", "green", "yellow", "blue", "magenta", "cyan", "gray",
	"darkgray", "lightred", "lightgreen", "lightyellow", "lightblue",
	"lightmagenta", "lightcyan", "white",
};

static const char * const attr_names[] = {
	"bold", "lowintensity", "underline", "blink", "reverse", "invisible", "keep"
};

static int parse_color(const char *str, int *val)
{
	char *end;
	long lval;
	int i;

	lval = strtol(str, &end, 10);
	if (*str && !*end) {
		if (lval < -2 || lval > 255) {
			error_msg("color value out of range");
			return 0;
		}
		*val = lval;
		return 1;
	}
	for (i = 0; i < ARRAY_COUNT(color_names); i++) {
		if (!strcasecmp(str, color_names[i])) {
			*val = i - 2;
			return 1;
		}
	}
	return 0;
}

static int parse_attr(const char *str, unsigned short *attr)
{
	int i;

	for (i = 0; i < ARRAY_COUNT(attr_names); i++) {
		if (!strcasecmp(str, attr_names[i])) {
			*attr |= 1 << i;
			return 1;
		}
	}
	return 0;
}

static int parse_term_color(struct term_color *color, char **strs)
{
	int i, count = 0;

	color->fg = -1;
	color->bg = -1;
	color->attr = 0;
	for (i = 0; strs[i]; i++) {
		const char *str = strs[i];
		int val;

		if (parse_color(str, &val)) {
			if (count > 1) {
				if (val == -2) {
					// "keep" is also a valid attribute
					color->attr |= ATTR_KEEP;
				} else {
					error_msg("too many colors");
					return 0;
				}
			} else {
				if (!count)
					color->fg = val;
				else
					color->bg = val;
				count++;
			}
		} else if (!parse_attr(str, &color->attr)) {
			error_msg("invalid color or attribute %s", str);
			return 0;
		}
	}
	return 1;
}

static void cmd_highlight(char **args)
{
	const char *pf = parse_args(&args, "", 2, -1);
	struct term_color color;

	if (!pf)
		return;

	if (parse_term_color(&color, args + 1))
		set_highlight_color(args[0], &color);
}

static void cmd_include(char **args)
{
	if (!parse_args(&args, "", 1, 1))
		return;
	read_config(args[0]);
}

static void cmd_insert(char **args)
{
	const char *pf = parse_args(&args, "ekm", 1, 1);
	const char *str = args[0];
	char *buf = NULL;

	if (!pf)
		return;

	if (strchr(pf, 'e')) {
		buf = shell_unescape(str);
		str = buf;
	}

	if (view->sel.blk)
		delete_ch();
	undo_merge = UNDO_MERGE_NONE;
	if (strchr(pf, 'k')) {
		int i;
		for (i = 0; str[i]; i++)
			insert_ch(str[i]);
	} else {
		int len = strlen(str);

		insert(str, len);
		if (strchr(pf, 'm'))
			move_offset(buffer_offset() + len);
	}
	free(buf);
}

static void cmd_insert_special(char **args)
{
	if (!parse_args(&args, "", 0, 0))
		return;
	input_special = INPUT_SPECIAL_UNKNOWN;
}

static void cmd_join(char **args)
{
	join_lines();
}

static void cmd_left(char **args)
{
	move_left(1);
}

static void cmd_line(char **args)
{
	const char *pf = parse_args(&args, "", 1, 1);
	int line;

	if (!pf)
		return;

	line = atoi(args[0]);
	if (line > 0)
		move_to_line(line);
}

static void cmd_new_line(char **args)
{
	const char *pf = parse_args(&args, "", 0, 0);

	if (!pf)
		return;

	new_line();
}

static void cmd_next(char **args)
{
	next_buffer();
}

static void cmd_open(char **args)
{
	const char *pf = parse_args(&args, "", 0, -1);
	int i, first = 1;

	if (!pf)
		return;

	if (!args[0]) {
		set_view(open_empty_buffer());
		return;
	}
	for (i = 0; args[i]; i++) {
		struct view *v = open_buffer(args[i], 0);
		if (v && first) {
			set_view(v);
			first = 0;
		}
	}
}

static void cmd_paste(char **args)
{
	paste();
}

static void cmd_pgdown(char **args)
{
	move_down(window->h - 1);
}

static void cmd_pgup(char **args)
{
	move_up(window->h - 1);
}

static void cmd_pop(char **args)
{
	pop_location();
}

static void cmd_prev(char **args)
{
	prev_buffer();
}

static void cmd_push(char **args)
{
	push_location();
}

static void cmd_quit(char **args)
{
	const char *pf = parse_args(&args, "f", 0, 0);
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
	const char *pf = parse_args(&args, "", 0, 1);
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
	redo(change_id);
}

static void cmd_repeat(char **args)
{
	const char *pf = parse_args(&args, "", 2, -1);
	const struct command *cmd;
	int count;

	if (!pf)
		return;

	count = atoi(args[0]);
	cmd = find_command(commands, args[1]);
	if (!cmd) {
		error_msg("No such command: %s", args[1]);
		return;
	}
	while (count-- > 0)
		cmd->cmd(args + 2);
}

static void cmd_replace(char **args)
{
	const char *pf = parse_args(&args, "bcgi", 2, 2);
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
	move_right(1);
}

static void cmd_run(char **args)
{
	const char *pf = parse_args(&args, "cdef=ijps", 1, -1);
	const char *compiler = NULL;
	unsigned int flags = 0;
	int quoted = 0;

	if (!pf)
		return;

	while (*pf) {
		switch (*pf) {
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

	if (flags & SPAWN_COLLECT_ERRORS && !compiler) {
		error_msg("Error format parser must be specified when collecting error messages.");
		return;
	}

	if (quoted) {
		struct parsed_command pc;
		char cmd[8192];
		char *word;

		if (args[1]) {
			error_msg("Too many arguments");
			return;
		}

		word = get_word_under_cursor();
		if (!word) {
			return;
		}

		snprintf(cmd, sizeof(cmd), args[0], word);
		free(word);
		if (parse_commands(&pc, cmd, 0)) {
			free_commands(&pc);
			return;
		}
		spawn(pc.argv, flags, compiler);
		free_commands(&pc);
	} else {
		spawn(args, flags, compiler);
	}
}

static int stat_changed(const struct stat *a, const struct stat *b)
{
	return a->st_mtime != b->st_mtime ||
		a->st_mode != b->st_mode ||
		a->st_dev != b->st_dev ||
		a->st_ino != b->st_ino;
}

static void cmd_save(char **args)
{
	const char *pf = parse_args(&args, "dfu", 0, 1);
	char *absolute = buffer->abs_filename;
	int force = 0;
	int newline = buffer->newline;
	mode_t old_mode = buffer->st.st_mode;
	struct stat st;

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
		absolute = path_absolute(args[0]);
		if (!absolute) {
			error_msg("Failed to make absolute path: %s", strerror(errno));
			return;
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
	} else {
		if (absolute == buffer->abs_filename && !force && stat_changed(&buffer->st, &st)) {
			error_msg("File has been modified by someone else. Use -f to force overwrite.");
			goto error;
		}
		if (S_ISDIR(st.st_mode)) {
			error_msg("Will not overwrite directory %s", absolute);
			goto error;
		}
		if (absolute != buffer->abs_filename && !force) {
			error_msg("Use -f to overwrite %s %s.", get_file_type(st.st_mode), absolute);
			goto error;
		}
	}
	if (save_buffer(absolute, newline))
		goto error;

	if (absolute != buffer->abs_filename) {
		free(buffer->filename);
		free(buffer->abs_filename);
		buffer->filename = xstrdup(args[0]);
		buffer->abs_filename = absolute;
	}
	if (!old_mode && !strcmp(buffer->options.filetype, "none")) {
		/* new file and most likely user has not changed the filetype */
		guess_filetype(buffer);
		filetype_changed(buffer);
		update_flags |= UPDATE_FULL;
	}
	return;
error:
	if (absolute != buffer->abs_filename)
		free(absolute);
}

static void do_search_next(char **args, enum search_direction dir)
{
	const char *pf = parse_args(&args, "w", 0, 0);

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
		search(pattern, REG_EXTENDED | REG_NEWLINE);
		history_add(&search_history, pattern);
		free(word);
		free(pattern);
	} else {
		input_mode = INPUT_SEARCH;
		search_init(dir);
		update_flags |= UPDATE_STATUS_LINE;
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
	search_next();
}

static void cmd_search_prev(char **args)
{
	search_prev();
}

static void cmd_select(char **args)
{
	const char *pf = parse_args(&args, "l", 0, 0);

	if (!pf)
		return;

	select_start(!!*pf);
}

static void cmd_set(char **args)
{
	const char *pf = parse_args(&args, "gl", 1, 2);
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
	const char *pf = parse_args(&args, "", 0, 1);
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

static const struct command syntax_commands[] = {
	{ "addc", NULL, syn_addc },
	{ "addr", NULL, syn_addr },
	{ "addw", NULL, syn_addw },
	{ "begin", NULL, syn_begin },
	{ "connect", NULL, syn_connect },
	{ "end", NULL, syn_end },
	{ "join", NULL, syn_join },
	{ NULL, NULL, NULL }
};

static void cmd_syntax(char **args)
{
	run_command(syntax_commands, args);
}

static void cmd_tag(char **args)
{
	const char *pf = parse_args(&args, "", 0, 1);

	if (!pf)
		return;

	if (args[0]) {
		goto_tag(args[0]);
	} else {
		char *word = get_word_under_cursor();
		if (!word) {
			return;
		}
		goto_tag(word);
		free(word);
	}
}

static void cmd_toggle(char **args)
{
	const char *pf = parse_args(&args, "gl", 1, 1);
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
	undo();
}

static void cmd_up(char **args)
{
	move_up(1);
}

const struct command commands[] = {
	{ "bind", NULL, cmd_bind },
	{ "bof", NULL, cmd_bof },
	{ "bol", NULL, cmd_bol },
	{ "cancel", NULL, cmd_cancel },
	{ "center-cursor", NULL, cmd_center_cursor },
	{ "clear", NULL, cmd_clear },
	{ "close", "cl", cmd_close },
	{ "command", NULL, cmd_command },
	{ "copy", NULL, cmd_copy },
	{ "cut", NULL, cmd_cut },
	{ "delete", NULL, cmd_delete },
	{ "delete-bol", NULL, cmd_delete_bol },
	{ "delete-eol", NULL, cmd_delete_eol },
	{ "down", NULL, cmd_down },
	{ "eof", NULL, cmd_eof },
	{ "eol", NULL, cmd_eol },
	{ "erase", NULL, cmd_erase },
	{ "erase-word", NULL, cmd_erase_word },
	{ "error", NULL, cmd_error },
	{ "errorfmt", NULL, cmd_errorfmt },
	{ "filetype", "ft", cmd_filetype },
	{ "highlight", "hi", cmd_highlight },
	{ "include", NULL, cmd_include },
	{ "insert", NULL, cmd_insert },
	{ "insert-special", NULL, cmd_insert_special },
	{ "join", NULL, cmd_join },
	{ "left", NULL, cmd_left },
	{ "line", NULL, cmd_line },
	{ "new-line", NULL, cmd_new_line },
	{ "next", NULL, cmd_next },
	{ "open", "o", cmd_open },
	{ "paste", NULL, cmd_paste },
	{ "pgdown", NULL, cmd_pgdown },
	{ "pgup", NULL, cmd_pgup },
	{ "pop", NULL, cmd_pop },
	{ "prev", NULL, cmd_prev },
	{ "push", NULL, cmd_push },
	{ "quit", "q", cmd_quit },
	{ "redo", NULL, cmd_redo },
	{ "repeat", NULL, cmd_repeat },
	{ "replace", "r", cmd_replace },
	{ "right", NULL, cmd_right },
	{ "run", NULL, cmd_run },
	{ "save", "s", cmd_save },
	{ "search-bwd", NULL, cmd_search_bwd },
	{ "search-fwd", NULL, cmd_search_fwd },
	{ "search-next", NULL, cmd_search_next },
	{ "search-prev", NULL, cmd_search_prev },
	{ "select", NULL, cmd_select },
	{ "set", NULL, cmd_set },
	{ "shift", NULL, cmd_shift },
	{ "syntax", "syn", cmd_syntax },
	{ "tag", "t", cmd_tag },
	{ "toggle", NULL, cmd_toggle },
	{ "undo", NULL, cmd_undo },
	{ "up", NULL, cmd_up },
	{ NULL, NULL, NULL }
};

void handle_binding(enum term_key_type type, unsigned int key)
{
	struct binding *b;

	pressed_types[nr_pressed_keys] = type;
	pressed_keys[nr_pressed_keys] = key;
	nr_pressed_keys++;

	list_for_each_entry(b, &bindings, node) {
		if (b->nr_keys < nr_pressed_keys)
			continue;

		if (memcmp(b->keys, pressed_keys, nr_pressed_keys * sizeof(pressed_keys[0])))
			continue;
		if (memcmp(b->types, pressed_types, nr_pressed_keys * sizeof(pressed_types[0])))
			continue;

		if (b->nr_keys == nr_pressed_keys) {
			undo_merge = UNDO_MERGE_NONE;
			handle_command(b->command);
			nr_pressed_keys = 0;
		}
		return;
	}
	nr_pressed_keys = 0;
}

const struct command *find_command(const struct command *cmds, const char *name)
{
	int i;

	for (i = 0; cmds[i].name; i++) {
		const struct command *cmd = &cmds[i];

		if (!strcmp(name, cmd->name))
			return cmd;
		if (cmd->short_name && !strcmp(name, cmd->short_name))
			return cmd;
	}
	return NULL;
}

static void run_command(const struct command *cmds, char **av)
{
	const struct command *cmd;

	if (!av[0]) {
		error_msg("Subcommand required");
		return;
	}
	cmd = find_command(cmds, av[0]);
	if (cmd) {
		current_command = cmd;
		cmd->cmd(av + 1);
		current_command = NULL;
	} else {
		error_msg("No such command: %s", av[0]);
	}
}

void handle_command(const char *cmd)
{
	struct parsed_command pc;
	int s, e;

	if (parse_commands(&pc, cmd, 0)) {
		free_commands(&pc);
		return;
	}

	s = 0;
	while (s < pc.count) {
		e = s;
		while (e < pc.count && pc.argv[e])
			e++;

		if (e > s)
			run_command(commands, pc.argv + s);

		s = e + 1;
	}

	free_commands(&pc);
}
