#include "alias.h"
#include "ptr-array.h"
#include "common.h"
#include "editor.h"
#include "commands.h"

struct alias {
	char *name;
	char *value;
};

static PTR_ARRAY(aliases);

static int validate_alias_name(const char *name)
{
	int i;

	for (i = 0; name[i]; i++) {
		char ch = name[i];
		if (!isalnum(ch) && ch != '-' && ch != '_')
			return 0;
	}
	return !!name[0];
}

void add_alias(const char *name, const char *value)
{
	struct alias *alias;
	int i;

	if (!validate_alias_name(name)) {
		error_msg("Invalid alias name '%s'", name);
		return;
	}
	if (find_command(commands, name)) {
		error_msg("Can't replace existing command %s with an alias", name);
		return;
	}

	/* replace existing alias */
	for (i = 0; i < aliases.count; i++) {
		alias = aliases.ptrs[i];
		if (!strcmp(alias->name, name)) {
			free(alias->value);
			alias->value = xstrdup(value);
			return;
		}
	}

	alias = xnew(struct alias, 1);
	alias->name = xstrdup(name);
	alias->value = xstrdup(value);
	ptr_array_add(&aliases, alias);

	if (editor_status != EDITOR_INITIALIZING)
		sort_aliases();
}

static int alias_cmp(const void *ap, const void *bp)
{
	const struct alias *a = *(const struct alias **)ap;
	const struct alias *b = *(const struct alias **)bp;
	return strcmp(a->name, b->name);
}

void sort_aliases(void)
{
	qsort(aliases.ptrs, aliases.count, sizeof(*aliases.ptrs), alias_cmp);
}

const char *find_alias(const char *name)
{
	int i;

	for (i = 0; i < aliases.count; i++) {
		const struct alias *alias = aliases.ptrs[i];
		if (!strcmp(alias->name, name))
			return alias->value;
	}
	return NULL;
}

void collect_aliases(const char *prefix)
{
	int i, prefix_len = strlen(prefix);

	for (i = 0; i < aliases.count; i++) {
		struct alias *alias = aliases.ptrs[i];
		if (!strncmp(prefix, alias->name, prefix_len))
			add_completion(xstrdup(alias->name));
	}
}
