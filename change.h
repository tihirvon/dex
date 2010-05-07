#ifndef CHANGE_H
#define CHANGE_H

enum undo_merge {
	UNDO_MERGE_NONE,
	UNDO_MERGE_INSERT,
	UNDO_MERGE_DELETE,
	UNDO_MERGE_BACKSPACE
};

void record_insert(unsigned int len);
void record_delete(char *buf, unsigned int len, int move_after);
void record_replace(char *deleted, unsigned int del_count, unsigned int ins_count);
void begin_change(enum undo_merge m);
void end_change(void);
void begin_change_chain(void);
void end_change_chain(void);
int undo(void);
int redo(unsigned int change_id);
void free_changes(void *head);

#endif
