#include "syntax.h"
#include "common.h"
#include "buffer.h"
#include "commands.h"

LIST_HEAD(syntaxes);

static struct syntax *cur_syntax;

unsigned int str_hash(const char *str)
{
	unsigned int hash = 0;
	int i;

	for (i = 0; str[i]; i++)
		hash = (hash << 5) - hash + str[i];
	return hash;
}

static int regexp_compile(regex_t *regex, const char *pattern, unsigned int flags)
{
	char error[1024];
	int err, cflags = REG_EXTENDED | REG_NEWLINE;

	if (flags & SYNTAX_FLAG_ICASE)
		cflags |= REG_ICASE;
	err = regcomp(regex, pattern, cflags);
	if (err) {
		regerror(err, regex, error, sizeof(error));
		regfree(regex);
		error_msg(error);
		return 0;
	}
	return 1;
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

static void add_node(union syntax_node *n, int type, const char *name, unsigned int flags)
{
	n->any.name = xstrdup(name);
	n->any.type = type;
	n->any.flags = flags;
	xrenew(cur_syntax->nodes, cur_syntax->nr_nodes + 1);
	cur_syntax->nodes[cur_syntax->nr_nodes++] = n;
}

void syn_begin(char **args)
{
	struct syntax_context *c;

	if (!parse_args(&args, "", 1, 1))
		return;

	if (cur_syntax) {
		error_msg("Syntax definition already started.");
		return;
	}
	cur_syntax = xnew0(struct syntax, 1);
	cur_syntax->name = xstrdup(args[0]);

	/* context "default" always exists */
	c = xnew0(struct syntax_context, 1);
	add_node((union syntax_node *)c, SYNTAX_NODE_CONTEXT, "default", 0);
}

void syn_end(char **args)
{
	if (!parse_args(&args, "", 0, 0))
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
	int i;

	for (i = 0; i < cur_syntax->nr_nodes; i++) {
		if (!strcmp(cur_syntax->nodes[i]->any.name, name))
			return cur_syntax->nodes[i];
	}
	return NULL;
}

void syn_addw(char **args)
{
	const char *pf = parse_args(&args, "i", 2, -1);
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
	} else {
		w = xnew0(struct syntax_word, 1);
		add_node((union syntax_node *)w, SYNTAX_NODE_WORD, name, flags);
		w->hash = xnew0(struct hash_word *, WORD_HASH_SIZE);
	}
	for (i = 1; args[i]; i++) {
		unsigned int hash_pos;
		struct hash_word *new, *next;
		int len = strlen(args[i]);
		char *str = xmalloc(len + 2);

		str[0] = len;
		memcpy(str + 1, args[i], len + 1);
		hash_pos = str_hash(str) % WORD_HASH_SIZE;

		next = w->hash[hash_pos];
		new = xnew(struct hash_word, 1);
		new->word = str;
		new->next = next;
		w->hash[hash_pos] = new;
	}
}

void syn_addr(char **args)
{
	const char *pf = parse_args(&args, "i", 2, 2);
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

	p = xnew0(struct syntax_pattern, 1);
	p->pattern = unescape_pattern(pattern);
	if (!regexp_compile(&p->regex, p->pattern, flags)) {
		free(p->pattern);
		free(p);
		return;
	}
	add_node((union syntax_node *)p, SYNTAX_NODE_PATTERN, name, flags);
}

void syn_addc(char **args)
{
	const char *pf = parse_args(&args, "i", 3, 3);
	const char *name;
	union syntax_node *n;
	struct syntax_context *c;
	unsigned int flags = 0;

	if (!pf)
		return;
	if (*pf)
		flags |= SYNTAX_FLAG_ICASE;
	name = args[0];

	n = find_syntax_node(name);
	if (n) {
		error_msg("Syntax node %s already exists.", name);
		return;
	}

	c = xnew0(struct syntax_context, 1);
	c->spattern = unescape_pattern(args[1]);
	c->epattern = unescape_pattern(args[2]);
	if (!regexp_compile(&c->sregex, c->spattern, flags)) {
		free(c->spattern);
		free(c);
		return;
	}
	if (!regexp_compile(&c->eregex, c->epattern, flags)) {
		regfree(&c->sregex);
		free(c->epattern);
		free(c->spattern);
		free(c);
		return;
	}

	add_node((union syntax_node *)c, SYNTAX_NODE_CONTEXT, name, flags);
}

void syn_connect(char **args)
{
	const char *name;
	union syntax_node *n;
	struct syntax_context *c;
	int i;

	if (!parse_args(&args, "", 2, -1))
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
	c = &n->context;
	for (i = 1; args[i]; i++) {
		n = find_syntax_node(args[i]);
		if (!n) {
			error_msg("No such syntax node %s", args[i]);
			continue;
		}
		xrenew(c->nodes, c->nr_nodes + 1);
		c->nodes[c->nr_nodes++] = n;
	}
}

static struct syntax_join *get_join(const char *name)
{
	int i, nr = cur_syntax->nr_join;

	for (i = 0; i < nr; i++) {
		if (!strcmp(cur_syntax->join[i].name, name))
			return &cur_syntax->join[i];
	}
	xrenew(cur_syntax->join, nr + 1);
	cur_syntax->join[nr].name = xstrdup(name);
	cur_syntax->join[nr].nodes = NULL;
	cur_syntax->join[nr].nr_nodes = 0;
	cur_syntax->nr_join++;
	return &cur_syntax->join[nr];
}

void syn_join(char **args)
{
	struct syntax_join *join;
	int i;

	if (!parse_args(&args, "", 2, -1))
		return;

	join = get_join(args[0]);
	for (i = 1; args[i]; i++) {
		union syntax_node *node = find_syntax_node(args[i]);

		if (!node) {
			error_msg("No such syntax node %s", args[i]);
			continue;
		}
		xrenew(join->nodes, join->nr_nodes + 1);
		join->nodes[join->nr_nodes++] = node;
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
