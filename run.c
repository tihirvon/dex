#include "run.h"
#include "buffer.h"
#include "editor.h"
#include "alias.h"
#include "commands.h"

const struct command *current_command;

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

void run_commands(const struct ptr_array *array)
{
	int s, e;

	s = 0;
	while (s < array->count) {
		e = s;
		while (e < array->count && array->ptrs[e])
			e++;

		if (e > s)
			run_command(commands, (char **)array->ptrs + s);

		s = e + 1;
	}
}

void run_command(const struct command *cmds, char **av)
{
	const struct command *cmd;

	if (!av[0]) {
		error_msg("Subcommand required");
		return;
	}
	cmd = find_command(cmds, av[0]);
	if (!cmd) {
		PTR_ARRAY(array);
		const char *alias;
		int i;

		if (cmds != commands) {
			error_msg("No such command: %s", av[0]);
			return;
		}
		alias = find_alias(av[0]);
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

		run_commands(&array);
		ptr_array_free(&array);
		return;
	}

	current_command = cmd;
	cmd->cmd(av + 1);
	current_command = NULL;
}

void handle_command(const char *cmd)
{
	PTR_ARRAY(array);

	undo_merge = UNDO_MERGE_NONE;

	if (parse_commands(&array, cmd)) {
		ptr_array_free(&array);
		return;
	}

	run_commands(&array);
	ptr_array_free(&array);
}
