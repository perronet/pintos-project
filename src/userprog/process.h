#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

#define ARG_MAX 100

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

struct args_struct
{
	char file_name[ARG_MAX];
	char file_args[ARG_MAX];
};

#endif /* userprog/process.h */
