#ifndef INPUT_SPECIAL_H
#define INPUT_SPECIAL_H

#include "term.h"

void special_input_activate(void);
int special_input_keypress(enum term_key_type type, unsigned int key, char *buf, int *count);
int special_input_misc_status(char *status);

#endif
