#ifndef EDITOR_H
#define EDITOR_H

#include "common.h"

extern char *home_dir;

const char *editor_file(const char *name);
void ui_start(int prompt);
void ui_end(void);

#endif
