#include "options.h"
#include "buffer.h"
#include "completion.h"
#include "filetype.h"
#include "common.h"
#include "editor.h"

struct global_options options = {
	.auto_indent = 1,
	.emulate_tab = 0,
	.expand_tab = 0,
	.file_history = 1,
	.indent_width = 8,
	.syntax = 1,
	.tab_width = 8,
	.text_width = 72,
	.ws_error = WSE_SPECIAL,

	.case_sensitive_search = CSS_TRUE,
	.display_special = 0,
	.esc_timeout = 100,
	.lock_files = 1,
	.newline = NEWLINE_UNIX,
	.scroll_margin = 0,
	.show_line_numbers = 0,
	.show_tab_bar = 1,
	.statusline_left = NULL,
	.statusline_right = NULL,
};

enum option_type {
	OPT_INT,
	OPT_STR,
	OPT_ENUM,
	OPT_FLAG,
};

static void default_int_set(int *local, int *global, int value)
{
	if (local)
		*local = value;
	if (global)
		*global = value;
	mark_everything_changed();
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
	mark_everything_changed();
}

static void statusline_set(char **local, char **global, const char *value)
{
	static const char chars[] = "fmryxXpEMnstu%";
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

static void syntax_set(int *local, int *global, int value)
{
	default_int_set(local, global, value);
	syntax_changed();
}

static void filetype_set(char **local, char **global, const char *value)
{
	if (strcmp(value, "none") && !is_ft(value)) {
		error_msg("No such file type %s", value);
		return;
	}
	free(*local);
	*local = xstrdup(value);

	filetype_changed();
}

#define default_enum_set default_int_set
#define default_flag_set default_int_set
#define default_bool_set default_enum_set

#define INT_OPT(_name, _local, _global, _offset, _min, _max, _set) { \
	.name = _name,						\
	.type = OPT_INT,					\
	.local = _local,					\
	.global = _global,					\
	.offset = _offset,					\
	.u = { .int_opt = {					\
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
	.u = { .enum_opt = {					\
		.set = _set,					\
		.values = _values,				\
	} },							\
}

#define FLAG_OPT(_name, _local, _global, _offset, _values, _set) { \
	.name = _name,						\
	.type = OPT_FLAG,					\
	.local = _local,					\
	.global = _global,					\
	.offset = _offset,					\
	.u = { .flag_opt = {					\
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
	.u = { .str_opt = {					\
		.set = _set,					\
	} },							\
}

#define L_OFFSET(member) offsetof(struct local_options, member)
#define G_OFFSET(member) offsetof(struct global_options, member)
/* make sure member is in all three structs */
#define C_OFFSET(member) offsetof(struct common_options, member) + L_OFFSET(member) - G_OFFSET(member)

#define L_INT(name, member, min, max, set) INT_OPT(name, 1, 0, L_OFFSET(member), min, max, set)
#define G_INT(name, member, min, max, set) INT_OPT(name, 0, 1, G_OFFSET(member), min, max, set)
#define C_INT(name, member, min, max, set) INT_OPT(name, 1, 1, C_OFFSET(member), min, max, set)

#define L_ENUM(name, member, values, set) ENUM_OPT(name, 1, 0, L_OFFSET(member), values, set)
#define G_ENUM(name, member, values, set) ENUM_OPT(name, 0, 1, G_OFFSET(member), values, set)
#define C_ENUM(name, member, values, set) ENUM_OPT(name, 1, 1, C_OFFSET(member), values, set)

#define L_FLAG(name, member, values, set) FLAG_OPT(name, 1, 0, L_OFFSET(member), values, set)
#define G_FLAG(name, member, values, set) FLAG_OPT(name, 0, 1, G_OFFSET(member), values, set)
#define C_FLAG(name, member, values, set) FLAG_OPT(name, 1, 1, C_OFFSET(member), values, set)

#define L_STR(name, member, set) STR_OPT(name, 1, 0, L_OFFSET(member), set)
#define G_STR(name, member, set) STR_OPT(name, 0, 1, G_OFFSET(member), set)
#define C_STR(name, member, set) STR_OPT(name, 1, 1, C_OFFSET(member), set)

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
			void (*set)(int *local, int *global, int value);
			const char **values;
		} flag_opt;
		struct {
			void (*set)(char **local, char **global, const char *value);
		} str_opt;
	} u;
};

static const char *bool_enum[] = { "false", "true", NULL };
static const char *newline_enum[] = { "unix", "dos", NULL };
const char *case_sensitive_search_enum[] = { "false", "true", "auto", NULL };
static const char *ws_error_values[] = {
	"trailing",
	"space-indent",
	"space-align",
	"tab-indent",
	"tab-after-indent",
	"special",
	NULL
};

static const struct option_description option_desc[] = {
	C_BOOL("auto-indent", auto_indent, default_bool_set),
	G_ENUM("case-sensitive-search", case_sensitive_search, case_sensitive_search_enum, default_enum_set),
	G_BOOL("display-special", display_special, default_bool_set),
	C_BOOL("emulate-tab", emulate_tab, default_bool_set),
	G_INT("esc-timeout", esc_timeout, 0, 2000, default_int_set),
	C_BOOL("expand-tab", expand_tab, default_bool_set),
	C_BOOL("file-history", file_history, default_bool_set),
	L_STR("filetype", filetype, filetype_set),
	C_INT("indent-width", indent_width, 1, 8, default_int_set),
	G_BOOL("lock-files", lock_files, default_bool_set),
	G_ENUM("newline", newline, newline_enum, default_enum_set),
	G_INT("scroll-margin", scroll_margin, 0, 100, default_int_set),
	G_BOOL("show-line-numbers", show_line_numbers, default_int_set),
	G_BOOL("show-tab-bar", show_tab_bar, default_int_set),
	G_STR("statusline-left", statusline_left, statusline_set),
	G_STR("statusline-right", statusline_right, statusline_set),
	C_BOOL("syntax", syntax, syntax_set),
	C_INT("tab-width", tab_width, 1, 8, default_int_set),
	C_INT("text-width", text_width, 1, 1000, default_int_set),
	C_FLAG("ws-error", ws_error, ws_error_values, default_flag_set),
};

static inline char *local_ptr(const struct option_description *desc, const struct local_options *opt)
{
	return (char *)opt + desc->offset;
}

static inline char *global_ptr(const struct option_description *desc)
{
	return (char *)&options + desc->offset;
}

static int parse_int(const char *value, int *ret)
{
	char *end;
	long val = strtol(value, &end, 10);

	if (!*value || *end)
		return 0;
	*ret = val;
	return 1;
}

static int parse_int_opt(const struct option_description *desc, const char *value, int *val)
{
	if (!value || !parse_int(value, val)) {
		error_msg("Integer value for %s expected.", desc->name);
		return 0;
	}
	if (*val < desc->u.int_opt.min || *val > desc->u.int_opt.max) {
		error_msg("Value for %s must be in %d-%d range.", desc->name,
			desc->u.int_opt.min, desc->u.int_opt.max);
		return 0;
	}
	return 1;
}

static int parse_enum(const struct option_description *desc, const char *value)
{
	int val, i;

	for (i = 0; desc->u.enum_opt.values[i]; i++) {
		if (!strcmp(desc->u.enum_opt.values[i], value))
			return i;
	}
	if (!parse_int(value, &val) || val < 0 || val >= i) {
		error_msg("Invalid value for %s.", desc->name);
		return -1;
	}
	return val;
}

static int parse_flags(const struct option_description *desc, const char *value)
{
	const char **values = desc->u.flag_opt.values;
	const char *ptr = value;
	int flags = 0;

	while (*ptr) {
		const char *end = strchr(ptr, ',');
		char *buf;
		int i, len;

		if (end) {
			len = end - ptr;
			end++;
		} else {
			len = strlen(ptr);
			end = ptr + len;
		}
		buf = xmemdup(ptr, len + 1);
		buf[len] = 0;
		ptr = end;

		for (i = 0; values[i]; i++) {
			if (!strcmp(buf, values[i])) {
				flags |= 1 << i;
				break;
			}
		}
		if (!values[i]) {
			int val, max = (1 << i) - 1;

			if (!parse_int(buf, &val) || val < 0 || val > max) {
				error_msg("Invalid value for %s.", desc->name);
				free(buf);
				return -1;
			}
			flags |= val;
		}
		free(buf);
	}
	return flags;
}

static void set_int_opt(const struct option_description *desc, const char *value, int *local, int *global)
{
	int val;

	if (parse_int_opt(desc, value, &val))
		desc->u.int_opt.set(local, global, val);
}

static void set_str_opt(const struct option_description *desc, const char *value, char **local, char **global)
{
	if (!value) {
		error_msg("String value for %s expected.", desc->name);
		return;
	}
	desc->u.str_opt.set(local, global, value);
}

static void set_enum_opt(const struct option_description *desc, const char *value, int *local, int *global)
{
	int val = 1;

	if (!value) {
		if (desc->u.enum_opt.values != bool_enum) {
			error_msg("Option %s is not boolean.", desc->name);
			return;
		}
	} else {
		val = parse_enum(desc, value);
	}
	if (val >= 0)
		desc->u.enum_opt.set(local, global, val);
}

static void set_flag_opt(const struct option_description *desc, const char *value, int *local, int *global)
{
	int flags;

	if (!value) {
		error_msg("No value given for %s.", desc->name);
		return;
	}

	flags = parse_flags(desc, value);
	if (flags >= 0)
		desc->u.flag_opt.set(local, global, flags);
}

static char *flags_to_string(const char **values, int flags)
{
	char buf[1024];
	char *ptr = buf;
	int i;

	if (!flags)
		return xstrdup("0");

	for (i = 0; values[i]; i++) {
		if (flags & (1 << i)) {
			int len = strlen(values[i]);
			memcpy(ptr, values[i], len);
			ptr += len;
			*ptr++ = ',';
		}
	}
	ptr[-1] = 0;
	return xstrdup(buf);
}

static char *option_to_string(const struct option_description *desc, const char *ptr)
{
	char buf[32];

	switch (desc->type) {
	case OPT_INT:
		snprintf(buf, sizeof(buf), "%d", *(int *)ptr);
		return xstrdup(buf);
	case OPT_STR:
		return xstrdup(*(const char **)ptr);
	case OPT_ENUM:
		return xstrdup(desc->u.enum_opt.values[*(int *)ptr]);
	case OPT_FLAG:
		return flags_to_string(desc->u.flag_opt.values, *(int *)ptr);
	}
	BUG("unreachable\n");
	return NULL;
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
		if (desc->local && buffer)
			flags |= OPT_LOCAL;
		if (desc->global)
			flags |= OPT_GLOBAL;
	}

	if (!buffer && (!flags || flags & OPT_LOCAL)) {
		error_msg("Local option can't be set in config file.");
		return;
	}

	if (flags & OPT_LOCAL)
		local = local_ptr(desc, &buffer->options);
	if (flags & OPT_GLOBAL)
		global = global_ptr(desc);

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
	case OPT_FLAG:
		set_flag_opt(desc, value, local, global);
		break;
	}
}

static int toggle(int value, const char **values)
{
	if (!values[++value])
		value = 0;
	return value;
}

void toggle_option(const char *name, int global, int verbose)
{
	const struct option_description *desc;
	char *ptr = NULL;

	desc = find_option(name, global ? OPT_GLOBAL : 0);
	if (!desc)
		return;
	if (desc->type != OPT_ENUM) {
		error_msg("Option %s is not toggleable.", name);
		return;
	}

	// toggle local value by default if option has both values
	if (!global && !desc->local)
		global = 1;

	if (global) {
		ptr = global_ptr(desc);
		desc->u.enum_opt.set(NULL, (int *)ptr, toggle(*(int *)ptr, desc->u.enum_opt.values));
	} else {
		ptr = local_ptr(desc, &buffer->options);
		desc->u.enum_opt.set((int *)ptr, NULL, toggle(*(int *)ptr, desc->u.enum_opt.values));
	}

	if (verbose) {
		char *str = option_to_string(desc, ptr);
		info_msg("%s = %s", desc->name, str);
		free(str);
	}
}

void toggle_option_values(const char *name, int global, int verbose, char **values)
{
	const struct option_description *desc;
	int i, count = count_strings(values);
	char *ptr = NULL;

	desc = find_option(name, global ? OPT_GLOBAL : 0);
	if (!desc)
		return;

	// toggle local value by default if option has both values
	if (!global && !desc->local)
		global = 1;

	if (desc->type == OPT_STR) {
		char *value;

		if (global)
			ptr = global_ptr(desc);
		else
			ptr = local_ptr(desc, &buffer->options);

		value = *(char **)ptr;
		for (i = 0; i < count; i++) {
			if (!strcmp(values[i], value))
				break;
		}
		if (i < count)
			i++;
		i %= count;

		if (global)
			desc->u.str_opt.set(NULL, (char **)ptr, values[i]);
		else
			desc->u.str_opt.set((char **)ptr, NULL, values[i]);
	} else {
		int *ints = xnew(int, count);
		int value;

		switch (desc->type) {
		case OPT_INT:
			for (i = 0; i < count; i++) {
				if (!parse_int_opt(desc, values[i], &ints[i])) {
					free(ints);
					return;
				}
			}
			break;
		case OPT_ENUM:
			for (i = 0; i < count; i++) {
				ints[i] = parse_enum(desc, values[i]);
				if (ints[i] < 0) {
					free(ints);
					return;
				}
			}
			break;
		case OPT_FLAG:
			for (i = 0; i < count; i++) {
				ints[i] = parse_flags(desc, values[i]);
				if (ints[i] < 0) {
					free(ints);
					return;
				}
			}
			break;
		}

		if (global)
			ptr = global_ptr(desc);
		else
			ptr = local_ptr(desc, &buffer->options);

		value = *(int *)ptr;
		for (i = 0; i < count; i++) {
			if (value == ints[i])
				break;
		}
		if (i < count)
			i++;
		i %= count;

		if (global)
			desc->u.int_opt.set(NULL, (int *)ptr, ints[i]);
		else
			desc->u.int_opt.set((int *)ptr, NULL, ints[i]);
		free(ints);
	}

	if (verbose) {
		char *str = option_to_string(desc, ptr);
		info_msg("%s = %s", desc->name, str);
		free(str);
	}
}

void collect_options(const char *prefix)
{
	int i;

	for (i = 0; i < ARRAY_COUNT(option_desc); i++) {
		const struct option_description *desc = &option_desc[i];
		if (str_has_prefix(desc->name, prefix))
			add_completion(xstrdup(desc->name));
	}
}

void collect_toggleable_options(const char *prefix)
{
	int i;

	for (i = 0; i < ARRAY_COUNT(option_desc); i++) {
		const struct option_description *desc = &option_desc[i];
		if (desc->type == OPT_ENUM && str_has_prefix(desc->name, prefix))
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

			if (desc->local) {
				ptr = local_ptr(desc, &buffer->options);
			} else {
				ptr = global_ptr(desc);
			}
			add_completion(option_to_string(desc, ptr));
		} else if (desc->type == OPT_ENUM) {
			/* complete possible values */
			int j;

			for (j = 0; desc->u.enum_opt.values[j]; j++) {
				if (str_has_prefix(desc->u.enum_opt.values[j], prefix))
					add_completion(xstrdup(desc->u.enum_opt.values[j]));
			}
		} else if (desc->type == OPT_FLAG) {
			/* complete possible values */
			const char *comma = strrchr(prefix, ',');
			int j, prefix_len = 0;

			if (comma)
				prefix_len = ++comma - prefix;
			for (j = 0; desc->u.flag_opt.values[j]; j++) {
				const char *str = desc->u.flag_opt.values[j];

				if (str_has_prefix(str, prefix + prefix_len)) {
					int str_len = strlen(str);
					char *completion = xmalloc(prefix_len + str_len + 1);
					memcpy(completion, prefix, prefix_len);
					memcpy(completion + prefix_len, str, str_len + 1);
					add_completion(completion);
				}
			}
		}
		break;
	}
}

void free_local_options(struct local_options *opt)
{
	int i;

	for (i = 0; i < ARRAY_COUNT(option_desc); i++) {
		const struct option_description *desc = &option_desc[i];

		if (desc->type != OPT_STR)
			continue;
		if (desc->local) {
			char **local = (char **)local_ptr(desc, opt);
			free(*local);
			*local = NULL;
		}
	}
}
