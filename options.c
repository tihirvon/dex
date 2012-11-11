#include "options.h"
#include "buffer.h"
#include "completion.h"
#include "filetype.h"
#include "common.h"
#include "regexp.h"
#include "error.h"

struct global_options options = {
	.auto_indent = 1,
	.detect_indent = 0,
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
	.tab_bar_max_components = 0,
	.tab_bar_width = 25,
	.vertical_tab_bar = 0,
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

static bool validate_statusline_format(const char *value)
{
	static const char chars[] = "fmryxXpEMnstu%";
	int i = 0;

	while (value[i]) {
		char ch = value[i++];

		if (ch == '%') {
			ch = value[i++];
			if (!ch) {
				error_msg("Format character expected after '%%'.");
				return false;
			}
			if (!strchr(chars, ch)) {
				error_msg("Invalid format character '%c'.", ch);
				return false;
			}
		}
	}
	return true;
}

static void syntax_set(int *local, int *global, int value)
{
	default_int_set(local, global, value);
	syntax_changed();
}

static bool validate_filetype(const char *value)
{
	if (strcmp(value, "none") && !is_ft(value)) {
		error_msg("No such file type %s", value);
		return false;
	}
	return true;
}

static void filetype_set(char **local, char **global, const char *value)
{
	free(*local);
	*local = xstrdup(value);

	filetype_changed();
}

static bool validate_regex(const char *value)
{
	if (value[0]) {
		regex_t re;
		if (!regexp_compile(&re, value, REG_NEWLINE | REG_NOSUB))
			return false;
		regfree(&re);
	}
	return true;
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

#define STR_OPT(_name, _local, _global, _offset, _set, _validate) { \
	.name = _name,						\
	.type = OPT_STR,					\
	.local = _local,					\
	.global = _global,					\
	.offset = _offset,					\
	.u = { .str_opt = {					\
		.set = _set,					\
		.validate = _validate,				\
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

#define L_STR(name, member, set, validate) STR_OPT(name, 1, 0, L_OFFSET(member), set, validate)
#define G_STR(name, member, set, validate) STR_OPT(name, 0, 1, G_OFFSET(member), set, validate)
#define C_STR(name, member, set, validate) STR_OPT(name, 1, 1, C_OFFSET(member), set, validate)

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
			bool (*validate)(const char *value);
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
	"auto-indent",
	NULL
};
static const char *detect_indent_values[] = { "1", "2", "3", "4", "5", "6", "7", "8", NULL };

static const struct option_description option_desc[] = {
	C_BOOL("auto-indent", auto_indent, default_bool_set),
	L_BOOL("brace-indent", brace_indent, default_bool_set),
	G_ENUM("case-sensitive-search", case_sensitive_search, case_sensitive_search_enum, default_enum_set),
	C_FLAG("detect-indent", detect_indent, detect_indent_values, default_flag_set),
	G_BOOL("display-special", display_special, default_bool_set),
	C_BOOL("emulate-tab", emulate_tab, default_bool_set),
	G_INT("esc-timeout", esc_timeout, 0, 2000, default_int_set),
	C_BOOL("expand-tab", expand_tab, default_bool_set),
	C_BOOL("file-history", file_history, default_bool_set),
	L_STR("filetype", filetype, filetype_set, validate_filetype),
	C_INT("indent-width", indent_width, 1, 8, default_int_set),
	L_STR("indent-regex", indent_regex, default_str_set, validate_regex),
	G_BOOL("lock-files", lock_files, default_bool_set),
	G_ENUM("newline", newline, newline_enum, default_enum_set),
	G_INT("scroll-margin", scroll_margin, 0, 100, default_int_set),
	G_BOOL("show-line-numbers", show_line_numbers, default_int_set),
	G_BOOL("show-tab-bar", show_tab_bar, default_int_set),
	G_STR("statusline-left", statusline_left, default_str_set, validate_statusline_format),
	G_STR("statusline-right", statusline_right, default_str_set, validate_statusline_format),
	C_BOOL("syntax", syntax, syntax_set),
	G_INT("tab-bar-max-components", tab_bar_max_components, 0, 10, default_int_set),
	G_INT("tab-bar-width", tab_bar_width, TAB_BAR_MIN_WIDTH, 100, default_int_set),
	C_INT("tab-width", tab_width, 1, 8, default_int_set),
	C_INT("text-width", text_width, 1, 1000, default_int_set),
	G_BOOL("vertical-tab-bar", vertical_tab_bar, default_int_set),
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

static bool parse_int_opt(const struct option_description *desc, const char *value, int *val)
{
	if (!str_to_int(value, val)) {
		error_msg("Integer value for %s expected.", desc->name);
		return false;
	}
	if (*val < desc->u.int_opt.min || *val > desc->u.int_opt.max) {
		error_msg("Value for %s must be in %d-%d range.", desc->name,
			desc->u.int_opt.min, desc->u.int_opt.max);
		return false;
	}
	return true;
}

static int parse_enum(const struct option_description *desc, const char *value)
{
	int val, i;

	for (i = 0; desc->u.enum_opt.values[i]; i++) {
		if (streq(desc->u.enum_opt.values[i], value))
			return i;
	}
	if (!str_to_int(value, &val) || val < 0 || val >= i) {
		error_msg("Invalid value for %s.", desc->name);
		return -1;
	}
	return val;
}

static int parse_flags(const struct option_description *desc, const char *value)
{
	const char **values = desc->u.flag_opt.values;
	const char *ptr = value;
	int val, flags = 0;

	// "0" is allowed for compatibility and is same as ""
	if (str_to_int(value, &val) && val == 0) {
		return 0;
	}
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
			if (streq(buf, values[i])) {
				flags |= 1 << i;
				break;
			}
		}
		if (!values[i]) {
			error_msg("Invalid flag '%s' for %s.", buf, desc->name);
			free(buf);
			return -1;
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
	if (desc->u.str_opt.validate(value))
		desc->u.str_opt.set(local, global, value);
}

static void set_enum_opt(const struct option_description *desc, const char *value, int *local, int *global)
{
	int val = parse_enum(desc, value);
	if (val >= 0)
		desc->u.enum_opt.set(local, global, val);
}

static void set_flag_opt(const struct option_description *desc, const char *value, int *local, int *global)
{
	int flags = parse_flags(desc, value);
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
	switch (desc->type) {
	case OPT_INT:
		return xsprintf("%d", *(int *)ptr);
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

static const struct option_description *find_option(const char *name)
{
	int i;

	for (i = 0; i < ARRAY_COUNT(option_desc); i++) {
		const struct option_description *desc = &option_desc[i];

		if (streq(name, desc->name))
			return desc;
	}
	return NULL;
}

static const struct option_description *must_find_option(const char *name)
{
	const struct option_description *desc = find_option(name);

	if (desc == NULL)
		error_msg("No such option %s", name);
	return desc;
}

static const struct option_description *must_find_global_option(const char *name)
{
	const struct option_description *desc = must_find_option(name);

	if (desc && !desc->global) {
		error_msg("Option %s is not global", name);
		return NULL;
	}
	return desc;
}

static void do_set_option(const struct option_description *desc, const char *value, bool local, bool global)
{
	void *l = NULL;
	void *g = NULL;

	if (local && !desc->local) {
		error_msg("Option %s is not local", desc->name);
		return;
	}
	if (global && !desc->global) {
		error_msg("Option %s is not global", desc->name);
		return;
	}
	if (!local && !global) {
		// set both by default
		if (desc->local)
			local = true;
		if (desc->global)
			global = true;
	}

	if (local)
		l = local_ptr(desc, &buffer->options);
	if (global)
		g = global_ptr(desc);

	switch (desc->type) {
	case OPT_INT:
		set_int_opt(desc, value, l, g);
		break;
	case OPT_STR:
		set_str_opt(desc, value, l, g);
		break;
	case OPT_ENUM:
		set_enum_opt(desc, value, l, g);
		break;
	case OPT_FLAG:
		set_flag_opt(desc, value, l, g);
		break;
	}
}

void set_option(const char *name, const char *value, bool local, bool global)
{
	const struct option_description *desc = must_find_option(name);

	if (!desc)
		return;
	do_set_option(desc, value, local, global);
}

void set_bool_option(const char *name, bool local, bool global)
{
	const struct option_description *desc = must_find_option(name);

	if (!desc)
		return;
	if (desc->type != OPT_ENUM || desc->u.enum_opt.values != bool_enum) {
		error_msg("Option %s is not boolean.", desc->name);
		return;
	}
	do_set_option(desc, "true", local, global);
}

static const struct option_description *find_toggle_option(const char *name, bool *global)
{
	const struct option_description *desc;

	if (*global)
		return must_find_global_option(name);
	// toggle local value by default if option has both values
	desc = must_find_option(name);
	if (desc && !desc->local)
		*global = true;
	return desc;
}

static int toggle(int value, const char **values)
{
	if (!values[++value])
		value = 0;
	return value;
}

void toggle_option(const char *name, bool global, bool verbose)
{
	const struct option_description *desc = find_toggle_option(name, &global);
	char *ptr = NULL;

	if (!desc)
		return;
	if (desc->type != OPT_ENUM) {
		error_msg("Option %s is not toggleable.", name);
		return;
	}

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

void toggle_option_values(const char *name, bool global, bool verbose, char **values)
{
	const struct option_description *desc = find_toggle_option(name, &global);
	int i, count = count_strings(values);
	char *ptr;

	if (!desc)
		return;

	if (global) {
		ptr = global_ptr(desc);
	} else {
		ptr = local_ptr(desc, &buffer->options);
	}
	if (desc->type == OPT_STR) {
		char *value;

		value = *(char **)ptr;
		for (i = 0; i < count; i++) {
			if (streq(values[i], value))
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
	const struct option_description *desc = find_option(name);

	if (!desc)
		return;

	if (!*prefix) {
		// complete value
		const char *ptr;

		if (desc->local) {
			ptr = local_ptr(desc, &buffer->options);
		} else {
			ptr = global_ptr(desc);
		}
		add_completion(option_to_string(desc, ptr));
	} else if (desc->type == OPT_ENUM) {
		// complete possible values
		int i;

		for (i = 0; desc->u.enum_opt.values[i]; i++) {
			if (str_has_prefix(desc->u.enum_opt.values[i], prefix))
				add_completion(xstrdup(desc->u.enum_opt.values[i]));
		}
	} else if (desc->type == OPT_FLAG) {
		// complete possible values
		const char *comma = strrchr(prefix, ',');
		int i, prefix_len = 0;

		if (comma)
			prefix_len = ++comma - prefix;
		for (i = 0; desc->u.flag_opt.values[i]; i++) {
			const char *str = desc->u.flag_opt.values[i];

			if (str_has_prefix(str, prefix + prefix_len)) {
				int str_len = strlen(str);
				char *completion = xmalloc(prefix_len + str_len + 1);
				memcpy(completion, prefix, prefix_len);
				memcpy(completion + prefix_len, str, str_len + 1);
				add_completion(completion);
			}
		}
	}
}

void free_local_options(struct local_options *opt)
{
	int i;

	for (i = 0; i < ARRAY_COUNT(option_desc); i++) {
		const struct option_description *desc = &option_desc[i];

		if (desc->local && desc->type == OPT_STR) {
			char **local = (char **)local_ptr(desc, opt);
			free(*local);
			*local = NULL;
		}
	}
}
