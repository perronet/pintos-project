#ifndef FILESYS_FSACCESS_H
#define FILESYS_FSACCESS_H
#include "threads/thread.h"

/* Synchronizes accesses to file system */
struct lock files_lock;

/* Represents an open file. */
struct file_descriptor 
{
  int fd_num;
  struct file *file_struct;
  tid_t owner;
  
  struct list_elem elem; 
};

void fsaccess_init (void);
struct file_descriptor * try_get_open_file (int fd_num);
int write_open_file (int fd_num, void *buffer, unsigned length);
void try_seek_open_file (int fd_num, unsigned position);
unsigned tell_open_file (int fd_num);
void try_close_open_file (int fd_num);

static struct list open_files;

#endif