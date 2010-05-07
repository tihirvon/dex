#ifndef CHANGE_H
#define CHANGE_H

enum change_merge {
	CHANGE_MERGE_NONE,
	CHANGE_MERGE_INSERT,
	CHANGE_MERGE_DELETE,
	CHANGE_MERGE_ERASE,
};

void record_insert(unsigned int len);
void record_delete(char *buf, unsigned int len, int move_after);
void record_replace(char *deleted, unsigned int del_count, unsigned int ins_count);
void begin_change(enum change_merge m);
void end_change(void);
void begin_change_chain(void);
void end_change_chain(void);
int undo(void);
int redo(unsigned int change_id);
void free_changes(void *head);

#endif
