#ifndef MSG_H
#define MSG_H

#include "libc.h"
#include "file-location.h"

struct message {
	char *msg;
	struct file_location *loc;
};

void pop_location(void);
struct message *new_message(const char *msg);
void add_message(struct message *m);
void current_message(bool save_location);
void next_message(void);
void prev_message(void);
void clear_messages(void);
int message_count(void);

#endif
