#ifndef MSG_H
#define MSG_H

#include "libc.h"

struct message {
	char *msg;
	char *file;
	union {
		char *pattern;
		struct {
			int line;
			int column;
		} location;
	} u;
	bool pattern_is_set;
};

void pop_location(void);
struct message *new_message(const char *msg);
void add_message(struct message *m);
void current_message(int save_location);
void next_message(void);
void prev_message(void);
void clear_messages(void);
int message_count(void);

#endif
