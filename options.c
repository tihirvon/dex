#include "common.h"
#include "options.h"
#include "buffer.h"
#include "commands.h"

struct global_options options = {
	.auto_indent = 1,
	.expand_tab = 0,
	.indent_width = 8,
	.tab_width = 8,
	.trim_whitespace = 1,

	.allow_incomplete_last_line = 0,
	.move_wraps = 1,
	.newline = NEWLINE_UNIX,

	.statusline_left = NULL,
	.statusline_right = NULL,
};

enum option_type {
	OPT_INT,
	OPT_STR,
	OPT_ENUM,
};

#define INT_OPT(_name, _local, _global, _offset, min, max) {	\
	.name = _name,						\
	.type = OPT_INT,					\
	.local = _local,					\
	.global = _global,					\
	.offset = _offset,					\
	{ {							\
		.int_min = min,					\
		.int_max = max,					\
	} },							\
}

#define ENUM_OPT(_name, _local, _global, _offset, _values) {	\
	.name = _name,						\
	.type = OPT_ENUM,					\
	.local = _local,					\
	.global = _global,					\
	.offset = _offset,					\
	{							\
		.enum_values = _values,				\
	},							\
}

#define STR_OPT(_name, _local, _global, _offset) {		\
	.name = _name,						\
	.type = OPT_STR,					\
	.local = _local,					\
	.global = _global,					\
	.offset = _offset,					\
}

#define L_OFFSET(member) offsetof(struct local_options, member)
#define G_OFFSET(member) offsetof(struct global_options, member)

#define L_INT(name, member, min, max) INT_OPT(name, 1, 0, L_OFFSET(member), min, max)
#define G_INT(name, member, min, max) INT_OPT(name, 0, 1, G_OFFSET(member), min, max)
#define C_INT(name, member, min, max) INT_OPT(name, 1, 1, G_OFFSET(member), min, max)

#define L_ENUM(name, member, enum_values) ENUM_OPT(name, 1, 0, L_OFFSET(member), enum_values)
#define G_ENUM(name, member, enum_values) ENUM_OPT(name, 0, 1, G_OFFSET(member), enum_values)
#define C_ENUM(name, member, enum_values) ENUM_OPT(name, 1, 1, G_OFFSET(member), enum_values)

#define L_STR(name, member) STR_OPT(name, 1, 0, L_OFFSET(member))
#define G_STR(name, member) STR_OPT(name, 0, 1, G_OFFSET(member))
#define C_STR(name, member) STR_OPT(name, 1, 1, G_OFFSET(member))

#define L_BOOL(name, member) L_ENUM(name, member, bool_enum)
#define G_BOOL(name, member) G_ENUM(name, member, bool_enum)
#define C_BOOL(name, member) C_ENUM(name, member, bool_enum)

struct option_description {
	const char *name;
	unsigned type : 6;
	unsigned local : 1;
	unsigned global : 1;
	unsigned offset : 16;
	union {
		struct {
			int int_min;
			int int_max;
		};
		const char **enum_values;
	};
};

static const char *bool_enum[] = { "false", "true", NULL };
static const char *newline_enum[] = { "unix", "dos", NULL };

static const struct option_description option_desc[] = {
	G_BOOL("allow-incomplete-last-line", allow_incomplete_last_line),
	C_BOOL("auto-indent", auto_indent),
	C_BOOL("expand-tab", expand_tab),
	C_INT("indent-width", indent_width, 1, 8),
	G_BOOL("move-wraps", move_wraps),
	G_ENUM("newline", newline, newline_enum),
	G_STR("statusline-left", statusline_left),
	G_STR("statusline-right", statusline_right),
	C_INT("tab-width", tab_width, 1, 8),
	C_BOOL("trim-whitespace", trim_whitespace),
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

static void set_int_opt(const struct option_description *desc, const char *value, int *local, int *global)
{
	int val;

	if (!value || !parse_int(value, &val)) {
		error_msg("Integer value for %s expected.", desc->name);
		return;
	}
	if (val < desc->int_min || val > desc->int_max) {
		error_msg("Value for %s must be in %d-%d range.", desc->name,
			desc->int_min, desc->int_max);
		return;
	}
	if (local)
		*local = val;
	if (global)
		*global = val;
}

static void set_str_opt(const struct option_description *desc, const char *value, char **local, char **global)
{
	if (local) {
		free(*local);
		*local = xstrdup(value);
	}
	if (global) {
		free(*global);
		*global = xstrdup(value);
	}
}

static void set_enum_opt(const struct option_description *desc, const char *value, int *local, int *global)
{
	int val;

	if (!value) {
		if (desc->enum_values != bool_enum) {
			error_msg("Invalid value for %s.", desc->name);
			return;
		}
		val = 1;
	} else {
		int i;

		for (i = 0; desc->enum_values[i]; i++) {
			if (!strcasecmp(desc->enum_values[i], value)) {
				val = i;
				goto set;
			}
		}
		if (!parse_int(value, &val) || val < 0 || val >= i) {
			error_msg("Invalid value for %s.", desc->name);
			return;
		}
	}
set:
	if (local)
		*local = val;
	if (global)
		*global = val;
}

static const struct option_description *find_option(const char *name, unsigned int flags)
{
	int i;

	for (i = 0; i < ARRAY_COUNT(option_desc); i++) {
		const struct option_description *desc = &option_desc[i];

		if (strcmp(name, desc->name))
			continue;
		if (flags & OPT_LOCAL && !desc->local) {
			error_msg("Option %s is not local", name);
			return NULL;
		}
		if (flags & OPT_GLOBAL && !desc->global) {
			error_msg("Option %s is not global", name);
			return NULL;
		}
		return desc;
	}
	error_msg("No such option %s", name);
	return NULL;
}

void set_option(const char *name, const char *value, unsigned int flags)
{
	const struct option_description *desc = find_option(name, flags);
	void *local = NULL;
	void *global = NULL;

	if (!desc)
		return;

	if (!(flags & (OPT_LOCAL | OPT_GLOBAL))) {
		if (desc->local)
			flags |= OPT_LOCAL;
		if (desc->global)
			flags |= OPT_GLOBAL;
	}

	if (flags & OPT_LOCAL)
		local = (char *)&buffer->options + desc->offset;
	if (flags & OPT_GLOBAL)
		global = (char *)&options + desc->offset;

	switch (desc->type) {
	case OPT_INT:
		set_int_opt(desc, value, local, global);
		break;
	case OPT_STR:
		set_str_opt(desc, value, local, global);
		break;
	case OPT_ENUM:
		set_enum_opt(desc, value, local, global);
		break;
	}
	update_flags |= UPDATE_FULL;
}

void toggle_option(const char *name, unsigned int flags)
{
	const struct option_description *desc = find_option(name, flags);

	if (!desc)
		return;
	if (desc->enum_values != bool_enum) {
		error_msg("Option %s is not boolean.", name);
		return;
	}

	if (!(flags & (OPT_LOCAL | OPT_GLOBAL))) {
		/* doesn't make sense to toggle both local and global value */
		if (desc->local)
			flags |= OPT_LOCAL;
		else if (desc->global)
			flags |= OPT_GLOBAL;
	}

	if (flags & OPT_LOCAL) {
		int *local = (int *)((char *)&buffer->options + desc->offset);
		*local = !*local;
	}
	if (flags & OPT_GLOBAL) {
		int *global = (int *)((char *)&options + desc->offset);
		*global = !*global;
	}
	update_flags |= UPDATE_FULL;
}

void collect_options(const char *prefix)
{
	int len = strlen(prefix);
	int i;

	for (i = 0; i < ARRAY_COUNT(option_desc); i++) {
		const struct option_description *desc = &option_desc[i];
		if (!strncmp(prefix, desc->name, len))
			add_completion(xstrdup(desc->name));
	}
}

void collect_option_values(const char *name, const char *prefix)
{
	int len = strlen(prefix);
	int i, j;

	for (i = 0; i < ARRAY_COUNT(option_desc); i++) {
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

void init_options(void)
{
	options.statusline_left = xstrdup(" %f %m");
	options.statusline_right = xstrdup(" %y,%X   %c %C   %p ");
}
