#ifndef BIND_H
#define BIND_H

#include "term.h"

void add_binding(const char *keys, const char *command);
void remove_binding(const char *keys);
void handle_binding(enum term_key_type type, unsigned int key);
int nr_pressed_keys(void);

#endif
