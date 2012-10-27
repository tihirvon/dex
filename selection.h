#ifndef SELECTION_H
#define SELECTION_H

#include "iter.h"

struct selection_info {
	struct block_iter si;
	long so;
	long eo;
	bool swapped;
};

void init_selection(struct selection_info *info);
long prepare_selection(void);
int get_nr_selected_lines(struct selection_info *info);
int get_nr_selected_chars(struct selection_info *info);

#endif
