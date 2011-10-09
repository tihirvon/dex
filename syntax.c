#include "syntax.h"
#include "state.h"
#include "color.h"
#include "ptr-array.h"
#include "editor.h"
#include "common.h"

static PTR_ARRAY(syntaxes);

unsigned int buf_hash(const char *str, unsigned int size)
{
	unsigned int i, hash = 0;

	for (i = 0; i < size; i++) {
		unsigned int ch = tolower(str[i]);
		hash = (hash << 5) - hash + ch;
	}
	return hash;
}

struct string_list *find_string_list(struct syntax *syn, const char *name)
{
	int i;

	for (i = 0; i < syn->string_lists.count; i++) {
		struct string_list *list = syn->string_lists.ptrs[i];
		if (!strcmp(list->name, name))
			return list;
	}
	return NULL;
}

struct state *find_state(struct syntax *syn, const char *name)
{
	int i;

	for (i = 0; i < syn->states.count; i++) {
		struct state *s = syn->states.ptrs[i];
		if (!strcmp(s->name, name))
			return s;
	}
	return NULL;
}

static int has_destination(enum condition_type type)
{
	switch (type) {
	case COND_RECOLOR:
	case COND_RECOLOR_BUFFER:
		return 0;
	default:
		return 1;
	}
}

static struct syntax *find_any_syntax(const char *name)
{
	int i;

	for (i = 0; i < syntaxes.count; i++) {
		struct syntax *syn = syntaxes.ptrs[i];
		if (!strcmp(syn->name, name))
			return syn;
	}
	return NULL;
}

static const char *fix_name(const char *name, const char *prefix)
{
	static char buf[64];
	snprintf(buf, sizeof(buf), "%s%s", prefix, name);
	return buf;
}

static void fix_action(struct syntax *syn, struct action *a, const char *prefix)
{
	if (a->destination.state) {
		const char *name = fix_name(a->destination.state->name, prefix);
		a->destination.state = find_state(syn, name);
	}
	if (a->emit_name)
		a->emit_name = xstrdup(a->emit_name);
}

static void fix_conditions(struct syntax *syn, struct state *s, struct state *rets, const char *prefix)
{
	int i;

	for (i = 0; i < s->conds.count; i++) {
		struct condition *c = s->conds.ptrs[i];
		fix_action(syn, &c->a, prefix);
		if (!c->a.destination.state && has_destination(c->type))
			c->a.destination.state = rets;
	}

	fix_action(syn, &s->a, prefix);
	if (!s->a.destination.state)
		s->a.destination.state = rets;
}

static struct state *merge(struct syntax *syn, struct syntax *subsyn, struct state *rets, const char *prefix)
{
	// NOTE: string_lists is owned by struct syntax so there's no need to
	// copy it.  Freeing struct condition does not free any string lists.
	struct ptr_array *states = &syn->states;
	int i, old_count = states->count;

	states->count += subsyn->states.count;
	if (states->count > states->alloc) {
		states->alloc = states->count;
		xrenew(states->ptrs, states->alloc);
	}
	memcpy(states->ptrs + old_count, subsyn->states.ptrs, sizeof(*states->ptrs) * subsyn->states.count);

	for (i = old_count; i < states->count; i++) {
		struct state *s = xmemdup(states->ptrs[i], sizeof(struct state));
		int j;

		states->ptrs[i] = s;
		s->name = xstrdup(fix_name(s->name, prefix));
		s->emit_name = xstrdup(s->emit_name);
		s->conds.ptrs = xmemdup(s->conds.ptrs, sizeof(void *) * s->conds.alloc);
		for (j = 0; j < s->conds.count; j++)
			s->conds.ptrs[j] = xmemdup(s->conds.ptrs[j], sizeof(struct condition));
	}

	for (i = old_count; i < states->count; i++)
		fix_conditions(syn, states->ptrs[i], rets, prefix);

	return states->ptrs[old_count];
}

static int finish_action(struct syntax *syn, struct action *a)
{
	char *sep, *name = a->destination.name;
	int errors = 0;

	if (!strcmp(name, "END")) {
		// this makes syntax subsyntax
		a->destination.state = NULL;
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
			a->destination.state = merge(syn, subsyn, rs, prefix);
		}
	} else {
		a->destination.state = find_state(syn, name);
		if (a->destination.state == NULL) {
			error_msg("No such state %s", name);
			errors++;
		}
	}
	free(name);
	return errors;
}

static int finish_condition(struct syntax *syn, struct condition *cond)
{
	int errors = 0;

	if (cond->type == COND_INLIST) {
		char *name = cond->u.cond_inlist.list_name;
		cond->u.cond_inlist.list = find_string_list(syn, name);
		if (cond->u.cond_inlist.list == NULL) {
			error_msg("No such list %s", name);
			errors++;
		} else {
			cond->u.cond_inlist.list->used = 1;
			if (cond->u.cond_inlist.list->hash)
				cond->type = COND_INLIST_HASH;
		}
		free(name);
	}
	if (has_destination(cond->type))
		errors += finish_action(syn, &cond->a);
	return errors;
}

static int finish_state(struct syntax *syn, struct state *s)
{
	int i, errors = 0;

	for (i = 0; i < s->conds.count; i++)
		errors += finish_condition(syn, s->conds.ptrs[i]);
	if (!s->a.destination.name) {
		error_msg("No default action in state %s", s->name);
		return ++errors;
	}
	return errors + finish_action(syn, &s->a);
}

static void visit(struct state *s)
{
	int i;

	if (s->visited)
		return;

	s->visited = 1;
	for (i = 0; i < s->conds.count; i++) {
		struct condition *cond = s->conds.ptrs[i];
		if (cond->a.destination.state)
			visit(cond->a.destination.state);
	}
	if (s->a.destination.state)
		visit(s->a.destination.state);
}

static void free_condition(struct condition *cond)
{
	free(cond->a.emit_name);
	free(cond);
}

static void free_state(struct state *s)
{
	int i;

	free(s->name);
	free(s->emit_name);
	for (i = 0; i < s->conds.count; i++)
		free_condition(s->conds.ptrs[i]);
	free(s->conds.ptrs);
	free(s->a.emit_name);
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

static void free_default_colors(char **strs)
{
	int i;
	for (i = 0; strs[i]; i++)
		free(strs[i]);
	free(strs);
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

	for (i = 0; i < syn->default_colors.count; i++)
		free_default_colors(syn->default_colors.ptrs[i]);
	free(syn->default_colors.ptrs);

	free(syn->name);
	free(syn);
}

void finalize_syntax(struct syntax *syn)
{
	int i, count, errors = 0;

	if (syn->states.count == 0) {
		error_msg("Empty syntax");
		errors++;
	}

	/*
	 * NOTE: merge() changes syn->states
	 */
	count = syn->states.count;
	for (i = 0; i < count; i++)
		errors += finish_state(syn, syn->states.ptrs[i]);

	if (syn->subsyntax) {
		if (syn->name[0] != '.') {
			error_msg("Subsyntax name must begin with '.'");
			errors++;
		}
	} else {
		if (syn->name[0] == '.') {
			error_msg("Only subsyntax name can begin with '.'");
			errors++;
		}
	}

	if (errors) {
		free_syntax(syn);
		syn = NULL;
		return;
	}

	// unused states and lists cause warning only
	visit(syn->states.ptrs[0]);
	for (i = 0; i < syn->states.count; i++) {
		struct state *s = syn->states.ptrs[i];
		if (!s->visited)
			error_msg("State %s is unreachable", s->name);
	}
	for (i = 0; i < syn->string_lists.count; i++) {
		struct string_list *list = syn->string_lists.ptrs[i];
		if (!list->used)
			error_msg("List %s never used", list->name);
	}

	ptr_array_add(&syntaxes, syn);
}

struct syntax *find_syntax(const char *name)
{
	struct syntax *syn = find_any_syntax(name);
	if (syn && syn->subsyntax)
		return NULL;
	return syn;
}

static const char *find_default_color(struct syntax *syn, const char *name)
{
	int i, j;

	for (i = 0; i < syn->default_colors.count; i++) {
		char **strs = syn->default_colors.ptrs[i];
		for (j = 1; strs[j]; j++) {
			if (!strcmp(strs[j], name))
				return strs[0];
		}
	}
	return NULL;
}

static void update_action_color(struct syntax *syn, struct action *a)
{
	const char *name = a->emit_name;
	const char *def;
	char full[64];

	if (!name)
		name = a->destination.state->emit_name;

	snprintf(full, sizeof(full), "%s.%s", syn->name, name);
	a->emit_color = find_color(full);
	if (a->emit_color)
		return;

	def = find_default_color(syn, name);
	if (!def)
		return;

	snprintf(full, sizeof(full), "%s.%s", syn->name, def);
	a->emit_color = find_color(full);
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

		for (j = 0; j < s->conds.count; j++) {
			struct condition *c = s->conds.ptrs[j];
			update_action_color(syn, &c->a);
		}
		update_action_color(syn, &s->a);
	}
}

void update_all_syntax_colors(void)
{
	int i;

	for (i = 0; i < syntaxes.count; i++)
		update_syntax_colors(syntaxes.ptrs[i]);
}
