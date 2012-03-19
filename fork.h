#ifndef FORK_H
#define FORK_H

#include "common.h"

static inline void close_on_exec(int fd)
{
	fcntl(fd, F_SETFD, FD_CLOEXEC);
}

int pipe_close_on_exec(int fd[2]);
int fork_exec(char **argv, int fd[3]);
int wait_child(int pid);

#endif
