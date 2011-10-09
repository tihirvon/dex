#include "state.h"
#include "syntax.h"
#include "color.h"
#include "command.h"
#include "editor.h"
#include "parse-args.h"
#include "config.h"
#include "common.h"

static void bitmap_set(unsigned char *bitmap, unsigned int idx)
{
	unsigned int byte = idx / 8;
	unsigned int bit = idx & 7;
	bitmap[byte] |= 1 << bit;
}

static void set_bits(unsigned char *bitmap, const unsigned char *pattern)
{
	int i;

	for (i = 0; pattern[i]; i++) {
		unsigned int ch = pattern[i];

		bitmap_set(bitmap, ch);
		if (pattern[i + 1] == '-' && pattern[i + 2]) {
			for (ch = ch + 1; ch <= pattern[i + 2]; ch++)
				bitmap_set(bitmap, ch);
			i += 2;
		}
	}
}

static struct syntax *current_syntax;
static struct state *current_state;

static int no_syntax(void)
{
	if (current_syntax)
		return 0;
	error_msg("No syntax started");
	return 1;
}

static int no_state(void)
{
	if (no_syntax())
		return 1;
	if (current_state)
		return 0;
	error_msg("No state started");
	return 1;
}

static struct condition *add_condition(enum condition_type type, const char *dest, const char *emit)
{
	struct condition *c;

	if (no_state())
		return NULL;

	c = xnew0(struct condition, 1);
	c->a.destination.name = dest ? xstrdup(dest) : NULL;
	c->a.emit_name = emit ? xstrdup(emit) : NULL;
	c->type = type;
	ptr_array_add(&current_state->conds, c);
	return c;
}

static void cmd_bufis(const char *pf, char **args)
{
	int icase = !!*pf;
	struct condition *c;
	const char *str = args[0];
	int len = strlen(str);

	if (len > ARRAY_COUNT(c->u.cond_bufis.str)) {
		error_msg("Maximum length of string is %lu bytes", ARRAY_COUNT(c->u.cond_bufis.str));
		return;
	}
	c = add_condition(COND_BUFIS, args[1], args[2]);
	if (c) {
		memcpy(c->u.cond_bufis.str, str, len);
		c->u.cond_bufis.len = len;
		c->u.cond_bufis.icase = icase;
	}
}

static void cmd_char(const char *pf, char **args)
{
	enum condition_type type = COND_CHAR;
	int not = 0;
	struct condition *c;

	while (*pf) {
		switch (*pf) {
		case 'b':
			type = COND_CHAR_BUFFER;
			break;
		case 'n':
			not = 1;
			break;
		}
		pf++;
	}

	c = add_condition(type, args[1], args[2]);
	if (!c)
		return;

	set_bits(c->u.cond_char.bitmap, args[0]);
	if (not) {
		int i;
		for (i = 0; i < ARRAY_COUNT(c->u.cond_char.bitmap); i++)
			c->u.cond_char.bitmap[i] = ~c->u.cond_char.bitmap[i];
	}
}

static void cmd_default(const char *pf, char **args)
{
	if (no_syntax())
		return;

	ptr_array_add(&current_syntax->default_colors, copy_string_array(args, count_strings(args)));
}

static void cmd_eat(const char *pf, char **args)
{
	if (no_state())
		return;

	current_state->a.destination.name = xstrdup(args[0]);
	current_state->a.emit_name = args[1] ? xstrdup(args[1]) : NULL;
	current_state = NULL;
}

static void cmd_list(const char *pf, char **args)
{
	const char *name = args[0];
	int argc = count_strings(++args);
	struct string_list *list;

	if (no_syntax())
		return;

	if (find_string_list(current_syntax, name)) {
		error_msg("List %s already exists.", name);
		return;
	}

	list = xnew0(struct string_list, 1);
	list->name = xstrdup(name);

	while (*pf) {
		switch (*pf) {
		case 'h':
			list->hash = 1;
			break;
		case 'i':
			list->icase = 1;
			break;
		}
		pf++;
	}

	if (list->hash) {
		int i;
		for (i = 0; i < argc; i++) {
			const char *str = args[i];
			int len = strlen(str);
			unsigned int idx = buf_hash(str, len) % ARRAY_COUNT(list->u.hash);
			struct hash_str *h = xmalloc(sizeof(struct hash_str *) + sizeof(int) + len);
			h->next = list->u.hash[idx];
			h->len = len;
			memcpy(h->str, str, len);
			list->u.hash[idx] = h;
		}
	} else {
		list->u.strings = copy_string_array(args, argc);
	}
	ptr_array_add(&current_syntax->string_lists, list);

	current_state = NULL;
}

static void cmd_inlist(const char *pf, char **args)
{
	const char *emit = args[2] ? args[2] : args[0];
	struct condition *c = add_condition(COND_INLIST, args[1], emit);

	if (c)
		c->u.cond_inlist.list_name = xstrdup(args[0]);
}

static void cmd_noeat(const char *pf, char **args)
{
	if (no_state())
		return;

	if (!strcmp(args[0], current_state->name)) {
		error_msg("Using noeat to to jump to parent state causes infinite loop");
		return;
	}

	current_state->a.destination.name = xstrdup(args[0]);
	current_state->a.emit_name = args[1] ? xstrdup(args[1]) : NULL;
	current_state->noeat = 1;
	current_state = NULL;
}

static void cmd_recolor(const char *pf, char **args)
{
	// if length is not specified then buffered bytes will be recolored
	enum condition_type type = COND_RECOLOR_BUFFER;
	struct condition *c;
	int len = 0;

	if (args[1]) {
		type = COND_RECOLOR;
		len = atoi(args[1]);
		if (len <= 0) {
			error_msg("number of bytes must be larger than 0");
			return;
		}
	}
	c = add_condition(type, NULL, args[0]);
	if (c && type == COND_RECOLOR)
		c->u.cond_recolor.len = len;
}

static void cmd_state(const char *pf, char **args)
{
	const char *name = args[0];
	const char *emit = args[1] ? args[1] : args[0];
	struct state *s;

	if (no_syntax())
		return;

	if (!strcmp(name, "END")) {
		error_msg("%s is reserved state name", name);
		return;
	}

	if (find_state(current_syntax, name)) {
		error_msg("State %s already exists.", name);
		return;
	}

	s = xnew0(struct state, 1);
	s->name = xstrdup(name);
	s->emit_name = xstrdup(emit);
	ptr_array_add(&current_syntax->states, s);

	current_state = s;
}

static void cmd_str(const char *pf, char **args)
{
	int icase = !!*pf;
	enum condition_type type = icase ? COND_STR_ICASE : COND_STR;
	const char *str = args[0];
	struct condition *c;
	int len = strlen(str);

	if (len > ARRAY_COUNT(c->u.cond_str.str)) {
		error_msg("Maximum length of string is %lu bytes", ARRAY_COUNT(c->u.cond_str.str));
		return;
	}

	// strings of length 2 are very common
	if (!icase && len == 2)
		type = COND_STR2;
	c = add_condition(type, args[1], args[2]);
	if (c) {
		memcpy(c->u.cond_str.str, str, len);
		c->u.cond_str.len = len;
	}
}

static void finish_syntax(void)
{
	finalize_syntax(current_syntax);
	current_syntax = NULL;
}

static void cmd_syntax(const char *pf, char **args)
{
	if (current_syntax)
		finish_syntax();

	current_syntax = xnew0(struct syntax, 1);
	current_syntax->name = xstrdup(args[0]);
	current_state = NULL;
}

static const struct command syntax_commands[] = {
	{ "bufis",	"i",	2,  3, cmd_bufis },
	{ "char",	"bn",	2,  3, cmd_char },
	{ "default",	"",	2, -1, cmd_default },
	{ "eat",	"",	1,  2, cmd_eat },
	{ "inlist",	"",	2,  3, cmd_inlist },
	{ "list",	"hi",	2, -1, cmd_list },
	{ "noeat",	"",	1,  1, cmd_noeat },
	{ "recolor",	"",	1,  2, cmd_recolor },
	{ "state",	"",	1,  2, cmd_state },
	{ "str",	"i",	2,  3, cmd_str },
	{ "syntax",	"",	1,  1, cmd_syntax },
	{ NULL,		NULL,	0,  0, NULL }
};

struct syntax *load_syntax_file(const char *filename, int must_exist)
{
	const char *slash = strrchr(filename, '/');
	const char *name = slash ? slash + 1 : filename;
	const char *saved_config_file = config_file;
	int saved_config_line = config_line;

	if (do_read_config(syntax_commands, filename, must_exist)) {
		config_file = saved_config_file;
		config_line = saved_config_line;
		return NULL;
	}
	if (current_syntax)
		finish_syntax();
	config_file = saved_config_file;
	config_line = saved_config_line;
	return find_syntax(name);
}
