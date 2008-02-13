#include "buffer.h"
#include "term.h"

#include <ctype.h>

struct binding {
	struct list_head node;
	enum term_key_type type;
	unsigned int key;
	char *command;
	struct binding *binding;
};

struct binding *uncompleted_binding;

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

static void cmd_bind(char **args)
{
	struct binding *b, *first = NULL, *prev = NULL;
	char *keys;
	const char *str;
	int count = 0;
	int i = 0;

	ARGC(2, 2);

	keys = xstrdup(args[0]);
	while (keys[i]) {
		enum term_key_type type;
		unsigned int key;
		int start = i;

		i++;
		while (keys[i] && keys[i] != ',')
			i++;
		if (keys[i] == ',')
			keys[i++] = 0;
		if (!parse_key(&type, &key, keys + start)) {
			free(keys);
			return;
		}
		count++;
	}
	if (!count) {
		free(keys);
		return;
	}

	str = keys;
	do {
		b = xnew(struct binding, 1);
		b->command = NULL;
		parse_key(&b->type, &b->key, str);
		str += strlen(str) + 1;
		if (!first)
			first = b;
		if (prev)
			prev->binding = b;
		prev = b;
	} while (--count);
	free(keys);

	b->command = xstrdup(args[1]);
	list_add_before(&first->node, &bindings);
}

static void cmd_delete(char **args)
{
	delete_ch();
}

static void cmd_backspace(char **args)
{
	backspace();
}

static void cmd_left(char **args)
{
	move_left(1);
}

static void cmd_right(char **args)
{
	move_right(1);
}

static void cmd_up(char **args)
{
	move_up(1);
}

static void cmd_down(char **args)
{
	move_down(1);
}

static void cmd_pgup(char **args)
{
	move_up(window->h - 1);
}

static void cmd_pgdown(char **args)
{
	move_down(window->h - 1);
}

static void cmd_save(char **args)
{
	save_buffer();
}

static void cmd_quit(char **args)
{
	running = 0;
}

static void cmd_prev(char **args)
{
	prev_buffer();
}

static void cmd_next(char **args)
{
	next_buffer();
}

static void cmd_undo(char **args)
{
	undo();
}

static void cmd_redo(char **args)
{
	redo();
}

static void cmd_paste(char **args)
{
	paste();
}

static void cmd_copy(char **args)
{
	copy_line();
}

static void cmd_cut(char **args)
{
	cut_line();
}

static void cmd_bol(char **args)
{
	move_bol();
}

static void cmd_eol(char **args)
{
	move_eol();
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

struct command commands[] = {
	{ "backspace", NULL, cmd_backspace },
	{ "bind", NULL, cmd_bind },
	{ "bol", NULL, cmd_bol },
	{ "copy", NULL, cmd_copy },
	{ "cut", NULL, cmd_cut },
	{ "debug_contents", NULL, cmd_debug_contents },
	{ "delete", NULL, cmd_delete },
	{ "down", NULL, cmd_down },
	{ "eol", NULL, cmd_eol },
	{ "left", NULL, cmd_left },
	{ "next", NULL, cmd_next },
	{ "paste", NULL, cmd_paste },
	{ "pgdown", NULL, cmd_pgdown },
	{ "pgup", NULL, cmd_pgup },
	{ "prev", NULL, cmd_prev },
	{ "quit", NULL, cmd_quit },
	{ "redo", NULL, cmd_redo },
	{ "right", NULL, cmd_right },
	{ "save", NULL, cmd_save },
	{ "undo", NULL, cmd_undo },
	{ "up", NULL, cmd_up },
	{ NULL, NULL, NULL }
};

static void handle_one(struct binding *b)
{
	if (b->command) {
		undo_merge = UNDO_MERGE_NONE;
		handle_command(b->command);
		uncompleted_binding = NULL;
	} else {
		uncompleted_binding = b->binding;
	}
}

void handle_binding(enum term_key_type type, unsigned int key)
{
	struct binding *b;

	if (uncompleted_binding) {
		if (type == KEY_NORMAL && key == 0x1b)
			return;
		if (uncompleted_binding->type == type && uncompleted_binding->key == key) {
			handle_one(uncompleted_binding);
			return;
		}
		uncompleted_binding = NULL;
	} else {
		list_for_each_entry(b, &bindings, node) {
			if (b->type == type && b->key == key) {
				handle_one(b);
				return;
			}
		}
	}
}
