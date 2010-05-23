#include "bind.h"
#include "common.h"
#include "editor.h"
#include "command.h"
#include "list.h"

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
	"sleft",
	"sright",
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
	if (!strcasecmp(str, "sp") || !strcasecmp(str, "space")) {
		*type = KEY_NORMAL;
		*key = ' ';
		return 1;
	}
	ch = toupper(str[1]);
	if (str[0] == '^' && len == 2) {
		if (ch >= 0x40 && ch < 0x60) {
			*type = KEY_NORMAL;
			*key = ch - 0x40;
			return 1;
		}
		if (ch == '?') {
			*type = KEY_NORMAL;
			*key = 0x7f;
			return 1;
		}
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

void add_binding(char *keys, const char *command)
{
	struct binding *b;
	int count = 0, i = 0;

	b = xnew(struct binding, 1);
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

	b->command = xstrdup(command);
	list_add_after(&b->node, &bindings);
	return;
error:
	free(b);
}

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
			handle_command(commands, b->command);
			nr_pressed_keys = 0;
		}
		return;
	}
	nr_pressed_keys = 0;
}
