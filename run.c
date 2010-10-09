#include "command.h"
#include "editor.h"
#include "alias.h"
#include "parse-args.h"
#include "change.h"
#include "config.h"

// commands that are allowed in config files
static const char *config_commands[] = {
	"alias",
	"bind",
	"cd",
	"errorfmt",
	"ft",
	"hi",
	"include",
	"load-syntax",
	"option",
	"set",
};

const struct command *current_command;

static int allowed_command(const char *name)
{
	int i;

	for (i = 0; i < ARRAY_COUNT(config_commands); i++) {
		if (!strcmp(name, config_commands[i]))
			return 1;
	}
	return 0;
}

const struct command *find_command(const struct command *cmds, const char *name)
{
	int i;

	for (i = 0; cmds[i].name; i++) {
		const struct command *cmd = &cmds[i];

		if (!strcmp(name, cmd->name))
			return cmd;
	}
	return NULL;
}

static void run_command(const struct command *cmds, char **av)
{
	const struct command *cmd = find_command(cmds, av[0]);
	const char *pf;
	char **args;

	if (!cmd) {
		PTR_ARRAY(array);
		const char *alias = find_alias(av[0]);
		int i;

		if (!alias) {
			error_msg("No such command or alias: %s", av[0]);
			return;
		}
		if (parse_commands(&array, alias)) {
			ptr_array_free(&array);
			return;
		}

		/* remove NULL */
		array.count--;

		for (i = 1; av[i]; i++)
			ptr_array_add(&array, xstrdup(av[i]));
		ptr_array_add(&array, NULL);

		run_commands(cmds, &array);
		ptr_array_free(&array);
		return;
	}

	if (config_file && cmds == commands && !allowed_command(cmd->name)) {
		error_msg("Command %s not allowed in config file.", cmd->name);
		return;
	}

	begin_change(CHANGE_MERGE_NONE);

	current_command = cmd;
	args = av + 1;
	pf = parse_args(args, cmd->flags, cmd->min_args, cmd->max_args);
	if (pf)
		cmd->cmd(pf, args);
	current_command = NULL;

	end_change();
}

void run_commands(const struct command *cmds, const struct ptr_array *array)
{
	int s, e;

	s = 0;
	while (s < array->count) {
		e = s;
		while (e < array->count && array->ptrs[e])
			e++;

		if (e > s)
			run_command(cmds, (char **)array->ptrs + s);

		s = e + 1;
	}
}

void handle_command(const struct command *cmds, const char *cmd)
{
	PTR_ARRAY(array);

	if (parse_commands(&array, cmd)) {
		ptr_array_free(&array);
		return;
	}

	run_commands(cmds, &array);
	ptr_array_free(&array);
}
