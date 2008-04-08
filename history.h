#ifndef HISTORY_H
#define HISTORY_H

#include "list.h"

struct history {
	struct list_head head;
	int nr_entries;
	int max_entries;
};

extern struct history search_history;
extern struct history command_history;

void history_add(struct history *h, const char *str);
void history_reset_search(void);
const char *history_search_forward(struct history *history, const char *text);
const char *history_search_backward(struct history *history);
void history_load(struct history *history, const char *filename);
void history_save(struct history *history, const char *filename);

#endif
