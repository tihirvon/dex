#ifndef INPUT_H
#define INPUT_H

#include "term.h"

// control key
#define CTRL(x) ((x) & ~0x40)

void keypress(enum term_key_type type, unsigned int key);

#endif
