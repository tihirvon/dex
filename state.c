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

unsigned int buf_hash(const char *str, unsigned int size)
{
	unsigned int i, hash = 0;

	for (i = 0; i < size; i++) {
		unsigned int ch = tolower(str[i]);
		hash = (hash << 5) - hash + ch;
	}
	return hash;
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

static struct syntax *find_any_syntax(const char *name)
{
	struct syntax *syn;

	list_for_each_entry(syn, &syntaxes, node) {
		if (!strcmp(syn->name, name))
			return syn;
	}
	return NULL;
}

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
	if (type == COND_NOEAT && !strcmp(dest, current_state->name)) {
		error_msg("Using noeat to to jump to parent state causes infinite loop");
		return NULL;
	}

	xrenew(current_state->conditions, current_state->nr_conditions + 1);
	c = &current_state->conditions[current_state->nr_conditions++];
	clear(c);
	c->destination.name = dest ? xstrdup(dest) : NULL;
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

static void cmd_recolor(const char *pf, char **args)
{
	struct condition *c;
	int len = atoi(args[1]);

	if (len <= 0) {
		error_msg("number of bytes must be larger than 0");
		return;
	}
	c = add_condition(COND_RECOLOR, NULL, args[0]);
	if (c)
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
	struct condition *c = add_condition(type, args[1], args[2]);

	if (c) {
		c->u.cond_str.str = xstrdup(args[0]);
		c->u.cond_str.len = strlen(args[0]);
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
	{ "list",	"hi",	2, -1, cmd_list },
	{ "listed",	"",	2,  3, cmd_listed },
	{ "noeat",	"",	1,  1, cmd_noeat },
	{ "recolor",	"",	2,  2, cmd_recolor },
	{ "state",	"",	1,  2, cmd_state },
	{ "str",	"i",	2,  3, cmd_str },
	{ "syntax",	"",	1,  1, cmd_syntax },
	{ NULL,		NULL,	0,  0, NULL }
};

static const char *fix_name(const char *name, const char *prefix)
{
	static char buf[64];
	snprintf(buf, sizeof(buf), "%s%s", prefix, name);
	return buf;
}

static void fix_conditions(struct syntax *syn, struct state *s, struct state *rets, const char *prefix)
{
	int i;

	for (i = 0; i < s->nr_conditions; i++) {
		struct condition *c = &s->conditions[i];
		if (c->destination.state) {
			const char *name = fix_name(c->destination.state->name, prefix);
			c->destination.state = find_state(syn, name);
		} else if (c->type != COND_RECOLOR) {
			c->destination.state = rets;
		}
		switch (c->type) {
		case COND_BUFIS:
			c->u.cond_bufis.str = xstrdup(c->u.cond_bufis.str);
			break;
		case COND_STR:
		case COND_STR_ICASE:
			c->u.cond_str.str = xstrdup(c->u.cond_str.str);
			break;
		case COND_BUFFER:
		case COND_CHAR:
		case COND_CHAR_BUFFER:
		case COND_EAT:
		case COND_LISTED:
		case COND_LISTED_HASH:
		case COND_NOEAT:
		case COND_RECOLOR:
			break;
		}
		if (c->emit_name)
			c->emit_name = xstrdup(c->emit_name);
	}
}

static struct state *merge(struct syntax *subsyn, struct state *rets, const char *prefix)
{
	// NOTE: string_lists is owned by struct syntax so there's no need to
	// copy it.  Freeing struct condition does not free any string lists.
	struct ptr_array *states = &current_syntax->states;
	int i, old_count = states->count;

	states->count += subsyn->states.count;
	if (states->count > states->alloc) {
		states->alloc = states->count;
		xrenew(states->ptrs, states->alloc);
	}
	memcpy(states->ptrs + old_count, subsyn->states.ptrs, sizeof(*states->ptrs) * subsyn->states.count);

	for (i = old_count; i < states->count; i++) {
		struct state *s = xmemdup(states->ptrs[i], sizeof(struct state));
		states->ptrs[i] = s;
		s->name = xstrdup(fix_name(s->name, prefix));
		s->emit_name = xstrdup(s->emit_name);
		s->conditions = xmemdup(s->conditions, sizeof(struct condition) * s->nr_conditions);
	}

	for (i = old_count; i < states->count; i++)
		fix_conditions(current_syntax, states->ptrs[i], rets, prefix);

	return states->ptrs[old_count];
}

static int finish_condition(struct syntax *syn, struct condition *cond)
{
	int errors = 0;
	char *name, *sep;

	if (cond->type == COND_LISTED) {
		name = cond->u.cond_listed.list_name;
		cond->u.cond_listed.list = find_string_list(syn, name);
		if (cond->u.cond_listed.list == NULL) {
			error_msg("No such list %s", name);
			errors++;
		} else if (cond->u.cond_listed.list->hash) {
			cond->type = COND_LISTED_HASH;
		}
		free(name);
	}

	name = cond->destination.name;
	if (!name)
		return errors;

	if (!strcmp(name, "END")) {
		// this makes syntax subsyntax
		cond->destination.state = NULL;
		free(name);
		syn->subsyntax = 1;
		return errors;
	}

	sep = strchr(name, ':');
	if (sep) {
		// subsyntax:returnstate
		const char *sub = name;
		const char *ret = sep + 1;
		struct syntax *subsyn;
		struct state *rs;

		*sep = 0;
		subsyn = find_any_syntax(sub);
		rs = find_state(syn, ret);
		if (!subsyn) {
			error_msg("No such syntax %s", sub);
			errors++;
		} else if (!subsyn->subsyntax) {
			error_msg("Syntax %s is not subsyntax", sub);
			errors++;
			subsyn = NULL;
		}
		if (!rs) {
			error_msg("No such state %s", ret);
			errors++;
		}

		if (subsyn && rs) {
			static int counter;
			char prefix[32];
			snprintf(prefix, sizeof(prefix), "%d-", counter++);
			cond->destination.state = merge(subsyn, rs, prefix);
		}
	} else {
		cond->destination.state = find_state(syn, name);
		if (cond->destination.state == NULL) {
			error_msg("No such state %s", name);
			errors++;
		}
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
		if (cond->destination.state)
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
	case COND_LISTED_HASH:
		break;
	case COND_NOEAT:
		break;
	case COND_RECOLOR:
		break;
	case COND_STR:
	case COND_STR_ICASE:
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

	if (list->hash) {
		for (i = 0; i < ARRAY_COUNT(list->u.hash); i++) {
			struct hash_str *h = list->u.hash[i];
			while (h) {
				struct hash_str *next = h->next;
				free(h);
				h = next;
			}
		}
	} else {
		for (i = 0; list->u.strings[i]; i++)
			free(list->u.strings[i]);
		free(list->u.strings);
	}
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
	int i, count, errors = 0;

	if (current_syntax->states.count == 0) {
		error_msg("Empty syntax");
		errors++;
	}

	/*
	 * NOTE: merge() changes current_syntax->states
	 */
	count = current_syntax->states.count;
	for (i = 0; i < count; i++)
		errors += finish_state(current_syntax, current_syntax->states.ptrs[i]);

	if (current_syntax->subsyntax) {
		if (current_syntax->name[0] != '.') {
			error_msg("Subsyntax name must begin with '.'");
			errors++;
		}
	} else {
		if (current_syntax->name[0] == '.') {
			error_msg("Only subsyntax name can begin with '.'");
			errors++;
		}
	}

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
	struct syntax *syn = find_any_syntax(name);
	if (syn && syn->subsyntax)
		return NULL;
	return syn;
}

void update_syntax_colors(struct syntax *syn)
{
	int i, j;

	if (syn->subsyntax) {
		// no point to update colors of a sub-syntax
		return;
	}
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
