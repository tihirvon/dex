#include "commands.h"
#include "cmdline.h"

struct {
	char *head;
	char *tail;
	char **matches;
	int count;
	int alloc;
	int idx;
} completion;

static void add_completion(const char *str)
{
	if (completion.count == completion.alloc) {
		completion.alloc = (completion.count + 8) & ~7;
		xrenew(completion.matches, completion.alloc);
	}
	completion.matches[completion.count++] = xstrdup(str);
}

static void collect_commands(const char *prefix)
{
	int prefix_len = strlen(prefix);
	int i;

	for (i = 0; commands[i].name; i++) {
		const struct command *c = &commands[i];

		if (!strncmp(prefix, c->name, prefix_len)) {
			add_completion(c->name);
			continue;
		}
		if (c->short_name && !strncmp(prefix, c->short_name, prefix_len))
			add_completion(c->name);
	}
}

static void collect_completions(struct parsed_command *pc, const char *str)
{
	const struct command *cmd;

	if (!pc->args_before_cursor) {
		collect_commands(str);
		return;
	}
	cmd = find_command(pc->argv[0]);
	if (cmd) {
	}
}

static void init_completion(void)
{
	struct parsed_command pc;
	const char *cmd = cmdline.buffer;
	char *str;
	int rc;

	rc = parse_commands(&pc, cmd, cmdline_pos);
	str = xstrndup(cmd + pc.comp_so, pc.comp_eo - pc.comp_so);
	d_print("%c %d %d %d |%s|\n", rc, pc.args_before_cursor, pc.comp_so, pc.comp_eo, str);

	completion.head = xstrndup(cmd, pc.comp_so);
	completion.tail = xstrdup(cmd + pc.comp_eo);

	collect_completions(&pc, str);
	free(str);
	free_commands(&pc);
}

void complete_command(void)
{
	const char *middle;
	char *str;
	int head_len, middle_len, tail_len;

	if (!completion.head)
		init_completion();
	if (!completion.count)
		return;

	middle = completion.matches[completion.idx];
	middle_len = strlen(middle);
	head_len = strlen(completion.head);
	tail_len = strlen(completion.tail);

	str = xmalloc(head_len + middle_len + tail_len + 2);
	memcpy(str, completion.head, head_len);
	memcpy(str + head_len, middle, middle_len);
	if (completion.count == 1) {
		str[head_len + middle_len] = ' ';
		middle_len++;
	}
	memcpy(str + head_len + middle_len, completion.tail, tail_len + 1);

	cmdline_set_text(str);
	cmdline_pos = head_len + middle_len;

	free(str);
	completion.idx = (completion.idx + 1) % completion.count;
	if (completion.count == 1)
		reset_completion();
}

void reset_completion(void)
{
	int i;

	free(completion.head);
	free(completion.tail);
	for (i = 0; i < completion.count; i++)
		free(completion.matches[i]);
	free(completion.matches);
	memset(&completion, 0, sizeof(completion));
}
