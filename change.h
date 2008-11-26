#ifndef CHANGE_H
#define CHANGE_H

void record_insert(unsigned int len);
void record_delete(char *buf, unsigned int len, int move_after);
void record_replace(char *deleted, unsigned int del_count, unsigned int ins_count);
void begin_change_chain(void);
void end_change_chain(void);
int undo(void);
int redo(unsigned int change_id);

#endif
