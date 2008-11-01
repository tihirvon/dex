#include "common.h"
#include "options.h"
#include "buffer.h"
#include "commands.h"
#include "filetype.h"

struct global_options options = {
	.auto_indent = 1,
	.expand_tab = 0,
	.indent_width = 8,
	.tab_width = 8,
	.text_width = 72,
	.trim_whitespace = 1,

	.allow_incomplete_last_line = 0,
	.move_wraps = 1,
	.newline = NEWLINE_UNIX,

	.statusline_left = NULL,
	.statusline_right = NULL,

	.display_special = 0,
	.esc_timeout = 100,
};

enum option_type {
	OPT_INT,
	OPT_STR,
	OPT_ENUM,
};

static void default_int_set(int *local, int *global, int value)
{
	if (local)
		*local = value;
	if (global)
		*global = value;
	update_flags |= UPDATE_FULL;
}

static void default_str_set(char **local, char **global, const char *value)
{
	if (local) {
		free(*local);
		*local = xstrdup(value);
	}
	if (global) {
		free(*global);
		*global = xstrdup(value);
	}
	update_flags |= UPDATE_FULL;
}

static void statusline_set(char **local, char **global, const char *value)
{
	static const char chars[] = "fmyxXcCp%";
	int i = 0;

	while (value[i]) {
		char ch = value[i++];

		if (ch == '%') {
			ch = value[i++];
			if (!ch) {
				error_msg("Format character expected after '%%'.");
				return;
			}
			if (!strchr(chars, ch)) {
				error_msg("Invalid format character '%c'.", ch);
				return;
			}
		}
	}
	default_str_set(local, global, value);
}

static void filetype_set(char **local, char **global, const char *value)
{
	if (strcmp(value, "none") && !is_ft(value)) {
		error_msg("No such file type %s", value);
		return;
	}
	free(*local);
	*local = xstrdup(value);

	filetype_changed(buffer);
	update_flags |= UPDATE_FULL;
}

#define default_enum_set default_int_set
#define default_bool_set default_enum_set

#define INT_OPT(_name, _local, _global, _offset, _min, _max, _set) { \
	.name = _name,						\
	.type = OPT_INT,					\
	.local = _local,					\
	.global = _global,					\
	.offset = _offset,					\
	{ .int_opt = {						\
		.set = _set,					\
		.min = _min,					\
		.max = _max,					\
	} },							\
}

#define ENUM_OPT(_name, _local, _global, _offset, _values, _set) { \
	.name = _name,						\
	.type = OPT_ENUM,					\
	.local = _local,					\
	.global = _global,					\
	.offset = _offset,					\
	{ .enum_opt = {						\
		.set = _set,					\
		.values = _values,				\
	} },							\
}

#define STR_OPT(_name, _local, _global, _offset, _set) {	\
	.name = _name,						\
	.type = OPT_STR,					\
	.local = _local,					\
	.global = _global,					\
	.offset = _offset,					\
	{ .str_opt = {						\
		.set = _set,					\
	} },							\
}

#define L_OFFSET(member) offsetof(struct local_options, member)
#define G_OFFSET(member) offsetof(struct global_options, member)

#define L_INT(name, member, min, max, set) INT_OPT(name, 1, 0, L_OFFSET(member), min, max, set)
#define G_INT(name, member, min, max, set) INT_OPT(name, 0, 1, G_OFFSET(member), min, max, set)
#define C_INT(name, member, min, max, set) INT_OPT(name, 1, 1, G_OFFSET(member), min, max, set)

#define L_ENUM(name, member, values, set) ENUM_OPT(name, 1, 0, L_OFFSET(member), values, set)
#define G_ENUM(name, member, values, set) ENUM_OPT(name, 0, 1, G_OFFSET(member), values, set)
#define C_ENUM(name, member, values, set) ENUM_OPT(name, 1, 1, G_OFFSET(member), values, set)

#define L_STR(name, member, set) STR_OPT(name, 1, 0, L_OFFSET(member), set)
#define G_STR(name, member, set) STR_OPT(name, 0, 1, G_OFFSET(member), set)
#define C_STR(name, member, set) STR_OPT(name, 1, 1, G_OFFSET(member), set)

#define L_BOOL(name, member, set) L_ENUM(name, member, bool_enum, set)
#define G_BOOL(name, member, set) G_ENUM(name, member, bool_enum, set)
#define C_BOOL(name, member, set) C_ENUM(name, member, bool_enum, set)

struct option_description {
	const char *name;
	unsigned type : 6;
	unsigned local : 1;
	unsigned global : 1;
	unsigned offset : 16;
	union {
		struct {
			void (*set)(int *local, int *global, int value);
			int min;
			int max;
		} int_opt;
		struct {
			void (*set)(int *local, int *global, int value);
			const char **values;
		} enum_opt;
		struct {
			void (*set)(char **local, char **global, const char *value);
		} str_opt;
	};
};

static const char *bool_enum[] = { "false", "true", NULL };
static const char *newline_enum[] = { "unix", "dos", NULL };

static const struct option_description option_desc[] = {
	G_BOOL("allow-incomplete-last-line", allow_incomplete_last_line, default_bool_set),
	C_BOOL("auto-indent", auto_indent, default_bool_set),
	G_BOOL("display-special", display_special, default_bool_set),
	C_BOOL("expand-tab", expand_tab, default_bool_set),
	G_INT("esc-timeout", esc_timeout, 0, 2000, default_int_set),
	L_STR("filetype", filetype, filetype_set),
	C_INT("indent-width", indent_width, 1, 8, default_int_set),
	G_BOOL("move-wraps", move_wraps, default_bool_set),
	G_ENUM("newline", newline, newline_enum, default_bool_set),
	G_STR("statusline-left", statusline_left, statusline_set),
	G_STR("statusline-right", statusline_right, statusline_set),
	C_INT("tab-width", tab_width, 1, 8, default_int_set),
	C_INT("text-width", text_width, 1, 1000, default_int_set),
	C_BOOL("trim-whitespace", trim_whitespace, default_bool_set),
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
	if (val < desc->int_opt.min || val > desc->int_opt.max) {
		error_msg("Value for %s must be in %d-%d range.", desc->name,
			desc->int_opt.min, desc->int_opt.max);
		return;
	}
	desc->int_opt.set(local, global, val);
}

static void set_str_opt(const struct option_description *desc, const char *value, char **local, char **global)
{
	if (!value) {
		error_msg("String value for %s expected.", desc->name);
		return;
	}
	desc->str_opt.set(local, global, value);
}

static void set_enum_opt(const struct option_description *desc, const char *value, int *local, int *global)
{
	int val;

	if (!value) {
		if (desc->enum_opt.values != bool_enum) {
			error_msg("Option %s is not boolean.", desc->name);
			return;
		}
		val = 1;
	} else {
		int i;

		for (i = 0; desc->enum_opt.values[i]; i++) {
			if (!strcasecmp(desc->enum_opt.values[i], value)) {
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
	desc->enum_opt.set(local, global, val);
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
}

static void toggle(int *valuep, const char **values)
{
	int value = *valuep + 1;
	if (!values[value])
		value = 0;
	*valuep = value;
}

void toggle_option(const char *name, unsigned int flags)
{
	const struct option_description *desc = find_option(name, flags);

	if (!desc)
		return;
	if (desc->type != OPT_ENUM) {
		error_msg("Option %s is not toggleable.", name);
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
		toggle(local, desc->enum_opt.values);
		desc->enum_opt.set(local, NULL, *local);
	}
	if (flags & OPT_GLOBAL) {
		int *global = (int *)((char *)&options + desc->offset);
		toggle(global, desc->enum_opt.values);
		desc->enum_opt.set(NULL, global, *global);
	}
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

void collect_toggleable_options(const char *prefix)
{
	int len = strlen(prefix);
	int i;

	for (i = 0; i < ARRAY_COUNT(option_desc); i++) {
		const struct option_description *desc = &option_desc[i];
		if (desc->type == OPT_ENUM && !strncmp(prefix, desc->name, len))
			add_completion(xstrdup(desc->name));
	}
}

void collect_option_values(const char *name, const char *prefix)
{
	int i;

	for (i = 0; i < ARRAY_COUNT(option_desc); i++) {
		const struct option_description *desc = &option_desc[i];

		if (strcmp(name, desc->name))
			continue;

		if (!*prefix) {
			/* complete value */
			const char *ptr;
			char buf[32];

			if (desc->local) {
				ptr = (const char *)&buffer->options + desc->offset;
			} else {
				ptr = (const char *)&options + desc->offset;
			}
			switch (desc->type) {
			case OPT_INT:
				snprintf(buf, sizeof(buf), "%d", *(int *)ptr);
				add_completion(xstrdup(buf));
				break;
			case OPT_STR:
				add_completion(xstrdup(*(const char **)ptr));
				break;
			case OPT_ENUM:
				add_completion(xstrdup(desc->enum_opt.values[*(int *)ptr]));
				break;
			}
		} else if (desc->type == OPT_ENUM) {
			/* complete possible values */
			int j, len = strlen(prefix);

			for (j = 0; desc->enum_opt.values[j]; j++) {
				if (!strncmp(prefix, desc->enum_opt.values[j], len))
					add_completion(xstrdup(desc->enum_opt.values[j]));
			}
			break;
		}
	}
}

void init_options(void)
{
	options.statusline_left = xstrdup(" %f %m");
	options.statusline_right = xstrdup(" %y,%X   %c %C   %p ");
}

void free_local_options(struct local_options *opt)
{
	int i;

	for (i = 0; i < ARRAY_COUNT(option_desc); i++) {
		const struct option_description *desc = &option_desc[i];

		if (desc->type != OPT_STR)
			continue;
		if (desc->local) {
			char **local = (char **)((char *)opt + desc->offset);
			free(*local);
			*local = NULL;
		}
	}
}
