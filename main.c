#include "editor.h"
#include "window.h"
#include "frame.h"
#include "term.h"
#include "config.h"
#include "color.h"
#include "syntax.h"
#include "alias.h"
#include "history.h"
#include "file-history.h"
#include "search.h"
#include "error.h"

#include <locale.h>
#include <langinfo.h>

static const char *builtin_rc =
// obvious bindings
"bind left left\n"
"bind right right\n"
"bind up up\n"
"bind down down\n"
"bind home bol\n"
"bind end eol\n"
"bind pgup pgup\n"
"bind pgdown pgdown\n"
"bind delete delete\n"
"bind ^\\[ unselect\n"
"bind ^Z suspend\n"
// backspace is either ^? or ^H
"bind ^\\? erase\n"
"bind ^H erase\n"
// there must be a way to get to the command line
"bind ^C command\n"
// initialize builtin colors
"hi\n"
// must initialize string options
"set statusline-left \" %f%s%m%r%s%M\"\n"
"set statusline-right \" %y,%X   %u   %E %n %t   %p \"\n";

static void handle_sigtstp(int signum)
{
	suspend();
}

static void handle_sigcont(int signum)
{
	if (!child_controls_terminal && editor_status != EDITOR_INITIALIZING) {
		term_raw();
		resize();
	}
}

static void handle_sigwinch(int signum)
{
	resized = true;
}

static void record_file_history(void)
{
	int i;

	for (i = 0; i < buffers.count; i++) {
		struct buffer *b = buffers.ptrs[i];
		if (b->options.file_history && b->abs_filename) {
			// choose first view
			struct view *v = b->views.ptrs[0];
			add_file_history(v->cy + 1, v->cx_char + 1, b->abs_filename);
		}
	}
}

static const char *opt_arg(const char *opt, const char *arg)
{
	if (arg == NULL) {
		fprintf(stderr, "missing argument for option %s\n", opt);
		exit(1);
	}
	return arg;
}

int main(int argc, char *argv[])
{
	const char *home = getenv("HOME");
	const char *tag = NULL;
	const char *rc = NULL;
	const char *command = NULL;
	char *command_history_filename;
	char *search_history_filename;
	char *editor_dir;
	bool use_terminfo = true;
	bool use_termcap = true;
	bool read_rc = true;
	int i;

	if (!home)
		home = "";
	home_dir = xstrdup(home);

	for (i = 1; i < argc; i++) {
		const char *opt = argv[i];

		if (opt[0] != '-' || !opt[1])
			break;
		if (!opt[2]) {
			switch (opt[1]) {
			case 'C':
				use_termcap = false;
				continue;
			case 'I':
				use_terminfo = false;
				continue;
			case 'R':
				read_rc = false;
				continue;
			case 't':
				tag = opt_arg(opt, argv[++i]);
				continue;
			case 'r':
				rc = opt_arg(opt, argv[++i]);
				continue;
			case 'c':
				command = opt_arg(opt, argv[++i]);
				continue;
			case 'V':
				printf("%s %s\nWritten by Timo Hirvonen\n", program, version);
				return 0;
			}
			if (opt[1] == '-') {
				i++;
				break;
			}
		}
		printf("Usage: %s [-R] [-V] [-c command] [-t tag] [-r rcfile] [file]...\n", argv[0]);
		return 1;
	}

	// create this early. needed if lock-files is true
	editor_dir = editor_file("");
	mkdir(editor_dir, 0755);
	free(editor_dir);

	setlocale(LC_CTYPE, "");
	charset = nl_langinfo(CODESET);
	if (streq(charset, "UTF-8"))
		term_utf8 = true;

	if (term_init(use_terminfo, use_termcap))
		error_msg("No terminal entry found.");

	exec_builtin_rc(builtin_rc);
	fill_builtin_colors();

	root_frame = new_frame();
	window = window_new();
	window->frame = root_frame;
	root_frame->window = window;

	if (read_rc) {
		if (rc) {
			read_config(commands, rc, true);
		} else {
			char *filename = editor_file("rc");
			if (read_config(commands, filename, false)) {
				free(filename);
				filename = xsprintf("%s/rc", pkgdatadir);
				read_config(commands, filename, true);
			}
			free(filename);
		}
	}

	update_all_syntax_colors();
	sort_aliases();

	/* Terminal does not generate signals for control keys. */
	set_signal_handler(SIGINT, SIG_IGN);
	set_signal_handler(SIGQUIT, SIG_IGN);
	set_signal_handler(SIGPIPE, SIG_IGN);

	/* Terminal does not generate signal for ^Z but someone can send
	 * us SIGTSTP nevertheless. SIGSTOP can't be caught.
	 */
	set_signal_handler(SIGTSTP, handle_sigtstp);

	set_signal_handler(SIGCONT, handle_sigcont);
	set_signal_handler(SIGWINCH, handle_sigwinch);

	load_file_history();
	command_history_filename = editor_file("command-history");
	search_history_filename = editor_file("search-history");
	history_load(&command_history, command_history_filename, command_history_size);
	history_load(&search_history, search_history_filename, search_history_size);
	if (search_history.count)
		search_set_regexp(search_history.ptrs[search_history.count - 1]);

	/* Initialize terminal but don't update screen yet.  Also display
	 * "Press any key to continue" prompt if there were any errors
	 * during reading configuration files.
	 */
	term_raw();
	if (nr_errors) {
		any_key();
		clear_error();
	}

	editor_status = EDITOR_RUNNING;

	for (; i < argc; i++)
		open_buffer(argv[i], false, NULL);
	if (window->views.count == 0)
		open_empty_buffer();
	set_view(window->views.ptrs[0]);

	if (command || tag)
		resize();

	if (command)
		handle_command(commands, command);
	if (tag) {
		const char *ptrs[3] = { "tag", tag, NULL };
		struct ptr_array array = { (void **)ptrs, 3, 3 };
		run_commands(commands, &array);
	}
	resize();
	main_loop();
	ui_end();
	history_save(&command_history, command_history_filename);
	history_save(&search_history, search_history_filename);
	free(command_history_filename);
	free(search_history_filename);
	record_file_history();
	save_file_history();
	return 0;
}
