#ifndef FILESYS_FSACCESS_H
#define FILESYS_FSACCESS_H
#include "threads/thread.h"
#include "filesys/file.h"

/* Synchronizes accesses to file system */
struct lock files_lock;

unsigned int fd_count;

/* Represents an open file. */
struct file_descriptor 
{
  int fd_num;
  struct file *open_file;
  tid_t owner;

  struct list_elem elem; 
};

void fsaccess_init (void);

bool create_file(const char *file, unsigned length);
bool remove_file(const char *file);

struct file_descriptor * get_file_descriptor (int fd_num);
int open_file(const char *filename);
int filelength_open_file (int fd_num);
int read_open_file(int fd_num, void *buffer, unsigned length);
int write_open_file (int fd_num, void *buffer, unsigned length);
void seek_open_file (int fd_num, unsigned position);
unsigned tell_open_file (int fd_num);
int memory_map_file (int fd_num, void *start_page);
void memory_unmap_file (int map_id);
void close_open_file (int fd_num);
void close_all_files(void);

void lock_fs (void);
void unlock_fs(void);
#endif