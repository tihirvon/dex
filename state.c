#include "state.h"
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

static void set_bits(unsigned char *bitmap, const char *pattern)
{
	int i;

	for (i = 0; pattern[i]; i++) {
		unsigned char ch = pattern[i];

		bitmap_set(bitmap, ch);
		if (pattern[i + 1] == '-' && pattern[i + 2]) {
			for (ch = ch + 1; ch <= pattern[i + 2]; ch++)
				bitmap_set(bitmap, ch);
			i += 2;
		}
	}
}

static struct string_list *find_string_list(struct syntax *syn, const char *name)
{
	int i;

	for (i = 0; i < syn->string_lists.count; i++) {
		struct string_list *list = syn->string_lists.ptrs[i];
		if (!strcmp(list->name, name))
			return list;
	}
	return NULL;
}

static struct state *find_state(struct syntax *syn, const char *name)
{
	int i;

	for (i = 0; i < syn->states.count; i++) {
		struct state *s = syn->states.ptrs[i];
		if (!strcmp(s->name, name))
			return s;
	}
	return NULL;
}

static int is_terminator(enum condition_type type)
{
	switch (type) {
	case COND_BUFFER:
	case COND_EAT:
	case COND_NOEAT:
		return 1;
	default:
		return 0;
	}
}

static LIST_HEAD(syntaxes);
static struct syntax *current_syntax;
static struct state *current_state;

static int no_syntax(void)
{
	if (current_syntax)
		return 0;
	error_msg("No syntax started");
	return 1;
}

static struct condition *add_condition(enum condition_type type, const char *dest, const char *emit)
{
	struct condition *c;

	if (no_syntax())
		return NULL;

	if (!current_state) {
		error_msg("No state started");
		return NULL;
	}

	xrenew(current_state->conditions, current_state->nr_conditions + 1);
	c = &current_state->conditions[current_state->nr_conditions++];
	clear(c);
	c->destination.name = xstrdup(dest);
	c->emit_name = emit ? xstrdup(emit) : NULL;
	c->type = type;

	if (is_terminator(type))
		current_state = NULL;
	return c;
}

static void cmd_buffer(const char *pf, char **args)
{
	add_condition(COND_BUFFER, args[0], NULL);
}

static void cmd_bufis(const char *pf, char **args)
{
	int icase = !!*pf;
	struct condition *c = add_condition(COND_BUFIS, args[1], args[2]);

	if (c) {
		c->u.cond_bufis.str = xstrdup(args[0]);
		c->u.cond_bufis.len = strlen(args[0]);
		c->u.cond_bufis.icase = icase;
	}
}

static void cmd_char(const char *pf, char **args)
{
	enum condition_type type = *pf ? COND_CHAR_BUFFER : COND_CHAR;
	struct condition *c = add_condition(type, args[1], args[2]);

	if (c)
		set_bits(c->u.cond_char.bitmap, args[0]);
}

static void cmd_eat(const char *pf, char **args)
{
	add_condition(COND_EAT, args[0], args[1]);
}

static void cmd_list(const char *pf, char **args)
{
	const char *name = args[0];
	int argc = count_strings(args);
	struct string_list *list;

	if (no_syntax())
		return;

	if (find_string_list(current_syntax, name)) {
		error_msg("List %s already exists.", name);
		return;
	}

	list = xnew(struct string_list, 1);
	list->name = xstrdup(name);
	list->strings = copy_string_array(args + 1, argc - 1);
	list->icase = !!*pf;
	ptr_array_add(&current_syntax->string_lists, list);

	current_state = NULL;
}

static void cmd_listed(const char *pf, char **args)
{
	const char *emit = args[2] ? args[2] : args[0];
	struct condition *c = add_condition(COND_LISTED, args[1], emit);

	if (c)
		c->u.cond_listed.list_name = xstrdup(args[0]);
}

static void cmd_noeat(const char *pf, char **args)
{
	add_condition(COND_NOEAT, args[0], NULL);
}

static void cmd_state(const char *pf, char **args)
{
	const char *name = args[0];
	const char *emit = args[1] ? args[1] : args[0];
	struct state *s;

	if (no_syntax())
		return;

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
	struct condition *c = add_condition(COND_STR, args[1], args[2]);

	if (c) {
		c->u.cond_str.str = xstrdup(args[0]);
		c->u.cond_str.len = strlen(args[0]);
		c->u.cond_str.icase = icase;
	}
}

static void finish_syntax(void);

static void cmd_syntax(const char *pf, char **args)
{
	if (current_syntax)
		finish_syntax();

	current_syntax = xnew0(struct syntax, 1);
	current_syntax->name = xstrdup(args[0]);
	current_state = NULL;
}

static const struct command syntax_commands[] = {
	{ "buffer",	"",	1,  1, cmd_buffer },
	{ "bufis",	"i",	2,  3, cmd_bufis },
	{ "char",	"b",	2,  3, cmd_char },
	{ "eat",	"",	1,  2, cmd_eat },
	{ "list",	"i",	2, -1, cmd_list },
	{ "listed",	"",	2,  3, cmd_listed },
	{ "noeat",	"",	1,  1, cmd_noeat },
	{ "state",	"",	1,  2, cmd_state },
	{ "str",	"i",	2,  3, cmd_str },
	{ "syntax",	"",	1,  1, cmd_syntax },
	{ NULL,		NULL,	0,  0, NULL }
};

static int finish_condition(struct syntax *syn, struct condition *cond)
{
	int errors = 0;
	char *name;

	if (cond->type == COND_LISTED) {
		name = cond->u.cond_listed.list_name;
		cond->u.cond_listed.list = find_string_list(syn, name);
		if (cond->u.cond_listed.list == NULL) {
			error_msg("No such list %s", name);
			errors++;
		}
		free(name);
	}

	name = cond->destination.name;
	cond->destination.state = find_state(syn, name);
	if (cond->destination.state == NULL) {
		error_msg("No such state %s", name);
		errors++;
	}
	free(name);
	return errors;
}

static int finish_state(struct syntax *syn, struct state *s)
{
	int i, errors = 0;

	for (i = 0; i < s->nr_conditions; i++)
		errors += finish_condition(syn, &s->conditions[i]);
	if (!s->nr_conditions) {
		error_msg("Empty state %s", s->name);
		errors++;
	} else if (!is_terminator(s->conditions[s->nr_conditions - 1].type)) {
		error_msg("No default condition in state %s", s->name);
		errors++;
	}
	return errors;
}

static void visit(struct state *s)
{
	int i;

	if (s->visited)
		return;

	s->visited = 1;
	for (i = 0; i < s->nr_conditions; i++) {
		struct condition *cond = &s->conditions[i];
		visit(cond->destination.state);
	}
}

static void free_condition(struct condition *cond)
{
	switch (cond->type) {
	case COND_BUFFER:
		break;
	case COND_BUFIS:
		free(cond->u.cond_bufis.str);
		break;
	case COND_CHAR:
		break;
	case COND_CHAR_BUFFER:
		break;
	case COND_EAT:
		break;
	case COND_LISTED:
		break;
	case COND_NOEAT:
		break;
	case COND_STR:
		free(cond->u.cond_str.str);
		break;
	}
	free(cond->emit_name);
}

static void free_state(struct state *s)
{
	int i;

	free(s->name);
	free(s->emit_name);
	for (i = 0; i < s->nr_conditions; i++)
		free_condition(&s->conditions[i]);
	free(s->conditions);
	free(s);
}

static void free_string_list(struct string_list *list)
{
	int i;

	for (i = 0; list->strings[i]; i++)
		free(list->strings[i]);
	free(list->strings);
	free(list->name);
	free(list);
}

static void free_syntax(struct syntax *syn)
{
	int i;

	for (i = 0; i < syn->states.count; i++)
		free_state(syn->states.ptrs[i]);
	free(syn->states.ptrs);

	for (i = 0; i < syn->string_lists.count; i++)
		free_string_list(syn->string_lists.ptrs[i]);
	free(syn->string_lists.ptrs);

	free(syn->name);
	free(syn);
}

struct syntax *load_syntax_file(const char *filename, int must_exist)
{
	const char *slash = strrchr(filename, '/');
	const char *name = slash ? slash + 1 : filename;

	if (read_config(syntax_commands, filename, must_exist))
		return NULL;
	if (current_syntax)
		finish_syntax();
	return find_syntax(name);
}

static void finish_syntax(void)
{
	int i, errors = 0;

	if (current_syntax->states.count == 0) {
		error_msg("Empty syntax");
		errors++;
	}

	for (i = 0; i < current_syntax->states.count; i++)
		errors += finish_state(current_syntax, current_syntax->states.ptrs[i]);
	if (errors) {
		free_syntax(current_syntax);
		current_syntax = NULL;
		return;
	}

	// unreachable states cause warning only
	visit(current_syntax->states.ptrs[0]);
	for (i = 0; i < current_syntax->states.count; i++) {
		struct state *s = current_syntax->states.ptrs[i];
		if (!s->visited)
			error_msg("State %s is unreachable", s->name);
	}

	list_add_before(&current_syntax->node, &syntaxes);
	current_syntax = NULL;
}

struct syntax *find_syntax(const char *name)
{
	struct syntax *syn;

	list_for_each_entry(syn, &syntaxes, node) {
		if (!strcmp(syn->name, name))
			return syn;
	}
	return NULL;
}

void update_syntax_colors(struct syntax *syn)
{
	int i, j;

	for (i = 0; i < syn->states.count; i++) {
		struct state *s = syn->states.ptrs[i];

		for (j = 0; j < s->nr_conditions; j++) {
			struct condition *cond = &s->conditions[j];
			const char *name = cond->emit_name;

			if (!name)
				name = cond->destination.state->emit_name;
			if (strchr(name, '.')) {
				cond->emit_color = find_color(name);
			} else {
				char buf[64];
				snprintf(buf, sizeof(buf), "%s.%s", syn->name, name);
				cond->emit_color = find_color(buf);
			}
		}
	}
}

void update_all_syntax_colors(void)
{
	struct syntax *syn;

	list_for_each_entry(syn, &syntaxes, node)
		update_syntax_colors(syn);
}
