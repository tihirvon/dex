#include "modes.h"

struct editor_mode_ops modes[] = {
	{ normal_mode_keypress },
	{ command_mode_keypress },
	{ search_mode_keypress },
};
