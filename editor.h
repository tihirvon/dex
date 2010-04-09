#ifndef EDITOR_H
#define EDITOR_H

#include "common.h"

enum editor_status {
	EDITOR_INITIALIZING,
	EDITOR_RUNNING,
	EDITOR_EXITING,
};

extern enum editor_status editor_status;
extern char *home_dir;

const char *editor_file(const char *name);
void error_msg(const char *format, ...) __FORMAT(1, 2);
void info_msg(const char *format, ...) __FORMAT(1, 2);
char get_confirmation(const char *choices, const char *format, ...) __FORMAT(2, 3);
void ui_start(int prompt);
void ui_end(void);

#endif
