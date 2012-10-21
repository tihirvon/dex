#ifndef ERROR_H
#define ERROR_H

#include "libc.h"

extern int nr_errors;
extern bool msg_is_error;
extern char error_buf[256];

void clear_error(void);
void error_msg(const char *format, ...) FORMAT(1);
void info_msg(const char *format, ...) FORMAT(1);

#endif
