#ifndef BIND_H
#define BIND_H

#include "term.h"

void add_binding(char *keys, const char *command);
void handle_binding(enum term_key_type type, unsigned int key);

#endif
