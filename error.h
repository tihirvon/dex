#ifndef ERROR_H
#define ERROR_H

#include "libc.h"

extern int msg_is_error;
extern char error_buf[256];

void clear_error(void);
void error_msg(const char *format, ...) FORMAT(1, 2);
void info_msg(const char *format, ...) FORMAT(1, 2);

#endif
