#include "common.h"
#include "options.h"
#include "buffer.h"
#include "commands.h"

struct options options = {
	.allow_incomplete_last_line = 0,
	.auto_indent = 1,
	.expand_tab = 0,
	.indent_width = 8,
	.move_wraps = 1,
	.tab_width = 8,
	.trim_whitespace = 1,
	.newline = NEWLINE_UNIX,
};

#define OPT_VAR_(prefix, var) prefix##var
#define OPT_VAR(prefix, var) OPT_VAR_(prefix, var)

#define INT_OPT(name, var)			{ name, OPT_INT, var, NULL }
#define ENUM_OPT(name, var, enum_values)	{ name, OPT_ENUM, var, enum_values }

#define BOOL_OPT(name, var) ENUM_OPT(name, var, bool_enum)

struct option_description {
	const char *name;
	enum {
		OPT_INT,
		OPT_ENUM,
	} type;
	void *value;
	const char **enum_values;
};

static const char *bool_enum[] = { "false", "true", NULL };
static const char *newline_enum[] = { "unix", "dos", NULL };

static const struct option_description option_desc[] = {
	BOOL_OPT("allow-incomplete-last-line", &options.allow_incomplete_last_line),
	BOOL_OPT("auto-indent", &options.auto_indent),
	BOOL_OPT("expand-tab", &options.expand_tab),
	INT_OPT("indent-width", &options.indent_width),
	BOOL_OPT("move-wraps", &options.move_wraps),
	ENUM_OPT("newline", &options.newline, newline_enum),
	INT_OPT("tab-width", &options.tab_width),
	BOOL_OPT("trim-whitespace", &options.trim_whitespace),
	{ NULL, 0, NULL, NULL }
};

static int parse_int(const char *value, int *ret)
{
	char *end;
	long val = strtol(value, &end, 10);

	if (!*value || *end)
		return 0;
	*ret = val;
	return 1;
}

static void set_int_opt(const struct option_description *desc, const char *value)
{
	int val;
	if (!value || !parse_int(value, &val)) {
		error_msg("Integer value for %s expected.", desc->name);
		return;
	}
	*(int *)desc->value = val;
}

static void set_enum_opt(const struct option_description *desc, const char *value)
{
	int i, val;
	if (!value) {
		if (desc->enum_values == bool_enum) {
			*(int *)desc->value = 1;
			return;
		}
		error_msg("Invalid value for %s.", desc->name);
		return;
	}
	for (i = 0; desc->enum_values[i]; i++) {
		if (!strcasecmp(desc->enum_values[i], value)) {
			*(int *)desc->value = i;
			return;
		}
	}
	if (!parse_int(value, &val) || val < 0 || val >= i) {
		error_msg("Invalid value for %s.", desc->name);
		return;
	}
	*(int *)desc->value = val;
}

void set_option(const char *name, const char *value)
{
	int i;

	for (i = 0; option_desc[i].name; i++) {
		const struct option_description *desc = &option_desc[i];
		if (strcmp(name, desc->name))
			continue;
		switch (desc->type) {
		case OPT_INT:
			set_int_opt(desc, value);
			break;
		case OPT_ENUM:
			set_enum_opt(desc, value);
			break;
		}
		return;
	}
	error_msg("No such option %s", name);
}

void collect_options(const char *prefix)
{
	int len = strlen(prefix);
	int i;

	for (i = 0; option_desc[i].name; i++) {
		const struct option_description *desc = &option_desc[i];
		if (!strncmp(prefix, desc->name, len))
			add_completion(xstrdup(desc->name));
	}
}

void collect_option_values(const char *name, const char *prefix)
{
	int len = strlen(prefix);
	int i, j;

	for (i = 0; option_desc[i].name; i++) {
		const struct option_description *desc = &option_desc[i];
		if (desc->type != OPT_ENUM || strcmp(name, desc->name))
			continue;
		for (j = 0; desc->enum_values[j]; j++) {
			if (!strncmp(prefix, desc->enum_values[j], len))
				add_completion(xstrdup(desc->enum_values[j]));
		}
		break;
	}
}
