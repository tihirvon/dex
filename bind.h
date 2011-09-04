#ifndef BIND_H
#define BIND_H

#include "term.h"

extern int nr_pressed_keys;

void add_binding(const char *keys, const char *command);
void handle_binding(enum term_key_type type, unsigned int key);

#endif
