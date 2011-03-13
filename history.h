#ifndef HISTORY_H
#define HISTORY_H

#include "ptr-array.h"

#define search_history_size 100
#define command_history_size 500

extern struct ptr_array search_history;
extern struct ptr_array command_history;

void history_add(struct ptr_array *history, const char *text, int max_entries);
void history_reset_search(void);
const char *history_search_forward(struct ptr_array *history, const char *text);
const char *history_search_backward(struct ptr_array *history);
void history_load(struct ptr_array *history, const char *filename, int max_entries);
void history_save(struct ptr_array *history, const char *filename);

#endif
