#include "syntax.h"
#include "common.h"
#include "buffer.h"
#include "commands.h"
#include "regexp.h"

static LIST_HEAD(syntaxes);
static struct syntax *cur_syntax;

union syntax_node **syntax_nodes;
int nr_syntax_nodes;

struct syntax_join *syntax_joins;
int nr_syntax_joins;

unsigned int buf_hash(const char *str, unsigned int size)
{
	unsigned int hash = 0;
	int i;

	for (i = 0; i < size; i++) {
		unsigned int ch = str[i];
		if (ch >= 'A' && ch <= 'Z')
			ch += 'a' - 'A';
		hash = (hash << 5) - hash + ch;
	}
	return hash;
}

static int syntax_regexp_compile(regex_t *regex, const char *pattern, unsigned int flags)
{
	int cflags = REG_EXTENDED | REG_NEWLINE;

	if (flags & SYNTAX_FLAG_ICASE)
		cflags |= REG_ICASE;

	if (regexp_compile(regex, pattern, cflags))
		return 1;

	regfree(regex);
	return 0;
}

static char *unescape_pattern(const char *str)
{
	int s, d, len = strlen(str);
	char *buf = xnew(char, len + 1);

	s = 0;
	d = 0;
	while (str[s]) {
		if (str[s] == '\\' && str[s + 1]) {
			if (str[s + 1] == 'n') {
				buf[d++] = '\n';
				s += 2;
				continue;
			}
			if (str[s + 1] == 't') {
				buf[d++] = '\t';
				s += 2;
				continue;
			}
			buf[d++] = str[s++];
		}
		buf[d++] = str[s++];
	}
	buf[d] = 0;
	return buf;
}

static inline int is_root(const union syntax_node *node)
{
	return node->any.type == SYNTAX_NODE_CONTEXT && !node->context.spattern;
}

static const char *get_real_node_name(const char *name)
{
	static char buf[64];
	const char *dot = strchr(name, '.');

	if (dot || !cur_syntax)
		return name;

	snprintf(buf, sizeof(buf), "%s.%s", cur_syntax->name, name);
	return buf;
}

static int too_many_nodes(void)
{
	static int warn = 1;

	if (nr_syntax_nodes < 1 << 14)
		return 0;
	if (warn)
		error_msg("Too many syntax nodes.");
	warn = 0;
	return 1;
}

static void add_node(union syntax_node *n, int type, const char *name, unsigned int flags)
{
	n->any.name = xstrdup(get_real_node_name(name));
	n->any.type = type;
	n->any.flags = flags;
	if (nr_syntax_nodes == ROUND_UP(nr_syntax_nodes, 32))
		xrenew(syntax_nodes, ROUND_UP(nr_syntax_nodes + 1, 32));
	syntax_nodes[nr_syntax_nodes++] = n;
}

void syn_begin(char **args)
{
	struct syntax_context *c;

	if (!parse_args(args, "", 1, 1))
		return;

	if (cur_syntax) {
		error_msg("Syntax definition already started.");
		return;
	}
	if (too_many_nodes())
		return;

	cur_syntax = xnew0(struct syntax, 1);
	cur_syntax->name = xstrdup(args[0]);

	/* context "root" always exists */
	c = xnew0(struct syntax_context, 1);
	cur_syntax->root = c;
	add_node((union syntax_node *)c, SYNTAX_NODE_CONTEXT, "root", 0);
}

void syn_end(char **args)
{
	if (!parse_args(args, "", 0, 0))
		return;

	if (!cur_syntax) {
		error_msg("No syntax definition has been started.");
		return;
	}
	list_add_before(&cur_syntax->node, &syntaxes);
	cur_syntax = NULL;
}

static union syntax_node *find_syntax_node(const char *name)
{
	const char *real_name = get_real_node_name(name);
	int i;

	for (i = 0; i < nr_syntax_nodes; i++) {
		if (!strcmp(syntax_nodes[i]->any.name, real_name))
			return syntax_nodes[i];
	}
	return NULL;
}

void syn_addw(char **args)
{
	const char *pf = parse_args(args, "i", 2, -1);
	const char *name;
	union syntax_node *n;
	struct syntax_word *w = NULL;
	unsigned int flags = 0;
	int i;

	if (!pf)
		return;
	if (*pf)
		flags |= SYNTAX_FLAG_ICASE;
	name = args[0];
	n = find_syntax_node(name);
	if (n && n->any.type != SYNTAX_NODE_WORD) {
		error_msg("Syntax node %s already exists and is not word list.", name);
		return;
	}

	// FIXME: validate words

	if (n) {
		w = &n->word;
		if (n->any.flags != flags) {
			error_msg("Syntax node %s previously defined with different flags.", name);
			return;
		}
	} else {
		if (too_many_nodes())
			return;

		w = xnew0(struct syntax_word, 1);
		add_node((union syntax_node *)w, SYNTAX_NODE_WORD, name, flags);
	}
	for (i = 1; args[i]; i++) {
		unsigned int hash_pos;
		struct hash_word *new, *next;
		int len = strlen(args[i]);
		char *str = xmalloc(len + 2);

		str[0] = len;
		memcpy(str + 1, args[i], len + 1);
		hash_pos = buf_hash(str + 1, len) % WORD_HASH_SIZE;

		next = w->hash[hash_pos];
		new = xnew(struct hash_word, 1);
		new->word = str;
		new->next = next;
		w->hash[hash_pos] = new;
	}
}

void syn_addr(char **args)
{
	const char *pf = parse_args(args, "i", 2, 2);
	const char *name;
	const char *pattern;
	union syntax_node *n;
	struct syntax_pattern *p;
	unsigned int flags = 0;

	if (!pf)
		return;
	if (*pf)
		flags |= SYNTAX_FLAG_ICASE;
	name = args[0];
	pattern = args[1];

	n = find_syntax_node(name);
	if (n) {
		error_msg("Syntax node %s already exists.", name);
		return;
	}

	if (too_many_nodes())
		return;

	p = xnew0(struct syntax_pattern, 1);
	p->pattern = unescape_pattern(pattern);
	if (!syntax_regexp_compile(&p->regex, p->pattern, flags)) {
		free(p->pattern);
		free(p);
		return;
	}
	add_node((union syntax_node *)p, SYNTAX_NODE_PATTERN, name, flags);
}

void syn_addc(char **args)
{
	const char *pf = parse_args(args, "hi", 3, 3);
	const char *name;
	union syntax_node *n;
	struct syntax_context *c;
	unsigned int flags = 0;

	if (!pf)
		return;
	while (*pf) {
		switch (*pf) {
		case 'h':
			flags |= SYNTAX_FLAG_HEREDOC;
			break;
		case 'i':
			flags |= SYNTAX_FLAG_ICASE;
			break;
		}
		pf++;
	}
	name = args[0];

	n = find_syntax_node(name);
	if (n) {
		error_msg("Syntax node %s already exists.", name);
		return;
	}

	if (too_many_nodes())
		return;

	c = xnew0(struct syntax_context, 1);
	c->spattern = unescape_pattern(args[1]);
	c->epattern = unescape_pattern(args[2]);
	if (!syntax_regexp_compile(&c->sregex, c->spattern, flags)) {
		free(c->spattern);
		free(c);
		return;
	}
	if (!(flags & SYNTAX_FLAG_HEREDOC) && !syntax_regexp_compile(&c->eregex, c->epattern, flags)) {
		regfree(&c->sregex);
		free(c->epattern);
		free(c->spattern);
		free(c);
		return;
	}

	add_node((union syntax_node *)c, SYNTAX_NODE_CONTEXT, name, flags);
}

static void connect_node(struct syntax_context *c, union syntax_node *node)
{
	xrenew(c->nodes, c->nr_nodes + 1);
	c->nodes[c->nr_nodes++] = node;
}

static void connect_by_name(struct syntax_context *c, const char *name)
{
	union syntax_node *n = find_syntax_node(name);

	if (!n) {
		error_msg("No such syntax node %s", name);
		return;
	}
	if (is_root(n)) {
		// connect all syntax nodes connected to root context of other syntax
		struct syntax_context *o = &n->context;
		int i;

		if (o == c) {
			error_msg("Can't connect root node to itself");
			return;
		}
		for (i = 0; i < o->nr_nodes; i++)
			connect_node(c, o->nodes[i]);
		return;
	}
	connect_node(c, n);
}

void syn_connect(char **args)
{
	const char *name;
	union syntax_node *n;
	int i;

	if (!parse_args(args, "", 2, -1))
		return;
	name = args[0];

	n = find_syntax_node(name);
	if (!n) {
		error_msg("No such syntax node %s", name);
		return;
	}
	if (n->any.type != SYNTAX_NODE_CONTEXT) {
		error_msg("Type of syntax node %s is not context.", name);
		return;
	}
	for (i = 1; args[i]; i++)
		connect_by_name(&n->context, args[i]);
}

static struct syntax_join *get_join(const char *name)
{
	int i;

	for (i = 0; i < nr_syntax_joins; i++) {
		if (!strcmp(syntax_joins[i].name, name))
			return &syntax_joins[i];
	}
	if (nr_syntax_joins == ROUND_UP(nr_syntax_joins, 32))
		xrenew(syntax_joins, ROUND_UP(nr_syntax_joins + 1, 32));
	syntax_joins[nr_syntax_joins].name = xstrdup(name);
	syntax_joins[nr_syntax_joins].items = NULL;
	syntax_joins[nr_syntax_joins].nr_items = 0;
	return &syntax_joins[nr_syntax_joins++];
}

static int parse_specifier(const char *name, enum syntax_node_specifier *specifier)
{
	static const char * const names[] = {
		"start",
		"end",
	};
	int i;

	for (i = 0; i < ARRAY_COUNT(names); i++) {
		if (!strcmp(names[i], name)) {
			*specifier = i + 1;
			return 1;
		}
	}
	error_msg("Invalid specifier %s.", name);
	return 0;
}

void syn_join(char **args)
{
	struct syntax_join *join;
	int i;

	if (!parse_args(args, "", 2, -1))
		return;

	join = get_join(get_real_node_name(args[0]));
	for (i = 1; args[i]; i++) {
		char *name = args[i];
		char *colon = strchr(name, ':');
		union syntax_node *node;
		enum syntax_node_specifier specifier = SPECIFIER_NORMAL;

		if (colon)
			*colon = 0;

		node = find_syntax_node(name);
		if (!node) {
			error_msg("No such syntax node %s", name);
			continue;
		}
		if (colon) {
			*colon = ':';
			if (!parse_specifier(colon + 1, &specifier))
				continue;
			if (specifier != SPECIFIER_NORMAL && node->any.type != SYNTAX_NODE_CONTEXT) {
				error_msg("Specifiers :start and :end are allowed for contexts only.");
				continue;
			}
		}
		if (join->nr_items == ROUND_UP(join->nr_items, 4))
			xrenew(join->items, ROUND_UP(join->nr_items + 1, 4));
		join->items[join->nr_items].node = node;
		join->items[join->nr_items].specifier = specifier;
		join->nr_items++;
	}
}

struct syntax *find_syntax(const char *name)
{
	struct syntax *s;

	list_for_each_entry(s, &syntaxes, node) {
		if (!strcmp(s->name, name))
			return s;
	}
	return NULL;
}
