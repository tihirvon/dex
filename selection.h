#ifndef SELECTION_H
#define SELECTION_H

#include "iter.h"

struct selection_info {
	struct block_iter si;
	unsigned int so;
	unsigned int eo;
	bool swapped;
};

void init_selection(struct selection_info *info);
unsigned int prepare_selection(void);
int get_nr_selected_lines(struct selection_info *info);
int get_nr_selected_chars(struct selection_info *info);

#endif
