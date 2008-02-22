#include "buffer.h"
#include "term.h"

#include <ctype.h>

#define MAX_KEYS 4

struct binding {
	struct list_head node;
	char *command;
	enum term_key_type types[MAX_KEYS];
	unsigned int keys[MAX_KEYS];
	int nr_keys;
};

int nr_pressed_keys;

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

static int parse_key(enum term_key_type *type, unsigned int *key, const char *str)
{
	int i, len = strlen(str);
	char ch;

	if (len == 1) {
		*type = KEY_NORMAL;
		*key = str[0];
		return 1;
	}
	ch = toupper(str[1]);
	if (str[0] == '^' && ch >= 0x40 && ch < 0x60 && len == 2) {
		*type = KEY_NORMAL;
		*key = ch - 0x40;
		return 1;
	}
	if (str[0] == 'M' && str[1] == '-' && len == 3) {
		*type = KEY_META;
		*key = str[2];
		return 1;
	}
	for (i = 0; i < NR_SKEYS; i++) {
		if (!strcasecmp(str, special_names[i])) {
			*type = KEY_SPECIAL;
			*key = i;
			return 1;
		}
	}
	return 0;
}

#define ARGC(min, max) \
	int argc = 0; \
	while (args[argc]) \
		argc++; \
	if (argc < min) { \
		d_print("not enough arguments\n"); \
		return; \
	} \
	if (max >= 0 && argc > max) { \
		d_print("too many arguments\n"); \
		return; \
	}

static void cmd_backspace(char **args)
{
	backspace();
}

static void cmd_bind(char **args)
{
	struct binding *b;
	char *keys;
	int count = 0, i = 0;

	ARGC(2, 2);

	b = xnew(struct binding, 1);
	b->command = xstrdup(args[1]);

	keys = args[0];
	while (keys[i]) {
		int start = i;

		if (count >= MAX_KEYS)
			goto error;

		i++;
		while (keys[i] && keys[i] != ',')
			i++;
		if (keys[i] == ',')
			keys[i++] = 0;
		if (!parse_key(&b->types[count], &b->keys[count], keys + start))
			goto error;
		count++;
	}
	b->nr_keys = count;
	if (!count)
		goto error;

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

static void cmd_close(char **args)
{
	if (buffer_modified(buffer)) {
		return;
	}

	remove_view();
}

static void cmd_command(char **args)
{
	input_mode = INPUT_COMMAND;
	update_flags |= UPDATE_STATUS_LINE;
}

static void cmd_copy(char **args)
{
	unsigned int len;

	undo_merge = UNDO_MERGE_NONE;
	len = prepare_selection();
	copy(len, view->sel_is_lines);
	select_end();
}

static void cmd_cut(char **args)
{
	unsigned int len;

	undo_merge = UNDO_MERGE_NONE;
	len = prepare_selection();
	cut(len, view->sel_is_lines);
	select_end();
}

static void cmd_debug_contents(char **args)
{
	struct block *blk;

	write(2, "\n--\n", 4);
	list_for_each_entry(blk, &buffer->blocks, node) {
		write(2, blk->data, blk->size);
		if (blk->node.next != &buffer->blocks)
			write(2, "-X-", 3);
	}
	write(2, "--\n", 3);
}

static void cmd_delete(char **args)
{
	delete_ch();
}

static void cmd_delete_bol(char **args)
{
	struct block_iter bi = view->cursor;
	unsigned int len = block_iter_bol(&bi);

	SET_CURSOR(bi);

	undo_merge = UNDO_MERGE_NONE;
	delete(len, 1);
}

static void cmd_delete_eol(char **args)
{
	struct block_iter bi = view->cursor;
	unsigned int len = count_bytes_eol(&bi) - 1;

	undo_merge = UNDO_MERGE_NONE;
	delete(len, 0);
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

static void cmd_left(char **args)
{
	move_left(1);
}

static void cmd_next(char **args)
{
	next_buffer();
}

static void cmd_open(char **args)
{
	struct view *v;

	ARGC(0, 1);
	v = open_buffer(args[0]);
	if (v)
		set_view(v);
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

static void cmd_prev(char **args)
{
	prev_buffer();
}

static void cmd_quit(char **args)
{
	running = 0;
}

static void cmd_redo(char **args)
{
	redo();
}

static void cmd_right(char **args)
{
	move_right(1);
}

static void cmd_save(char **args)
{
	save_buffer();
}

static void cmd_select(char **args)
{
	int is_lines = 0;
	int i = 0;

	ARGC(0, 1);
	while (i < argc) {
		const char *arg = args[i++];

		if (!strcmp(arg, "--lines")) {
			is_lines = 1;
			continue;
		}
	}
	select_start(is_lines);
}

static void cmd_undo(char **args)
{
	undo();
}

static void cmd_up(char **args)
{
	move_up(1);
}

struct command commands[] = {
	{ "backspace", NULL, cmd_backspace },
	{ "bind", NULL, cmd_bind },
	{ "bof", NULL, cmd_bof },
	{ "bol", NULL, cmd_bol },
	{ "cancel", NULL, cmd_cancel },
	{ "close", NULL, cmd_close },
	{ "command", NULL, cmd_command },
	{ "copy", NULL, cmd_copy },
	{ "cut", NULL, cmd_cut },
	{ "debug_contents", NULL, cmd_debug_contents },
	{ "delete", NULL, cmd_delete },
	{ "delete_bol", NULL, cmd_delete_bol },
	{ "delete_eol", NULL, cmd_delete_eol },
	{ "down", NULL, cmd_down },
	{ "eof", NULL, cmd_eof },
	{ "eol", NULL, cmd_eol },
	{ "left", NULL, cmd_left },
	{ "next", NULL, cmd_next },
	{ "open", NULL, cmd_open },
	{ "paste", NULL, cmd_paste },
	{ "pgdown", NULL, cmd_pgdown },
	{ "pgup", NULL, cmd_pgup },
	{ "prev", NULL, cmd_prev },
	{ "quit", NULL, cmd_quit },
	{ "redo", NULL, cmd_redo },
	{ "right", NULL, cmd_right },
	{ "save", NULL, cmd_save },
	{ "select", NULL, cmd_select },
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
