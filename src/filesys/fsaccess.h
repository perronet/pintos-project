#ifndef FILESYS_FSACCESS_H
#define FILESYS_FSACCESS_H
#include "threads/thread.h"
#include "filesys/file.h"
#include "lib/string.h"

#define FS_DEBUG //TODO comment to enable fine-grained synch on W/R

/* Synchronizes accesses to file system */
struct lock files_lock;


unsigned int fd_count;

/* Represents an open file. */
struct file_descriptor 
{
  int fd_num;
  struct file *open_file;
  struct dir *open_dir;
  tid_t owner;
  bool is_dir;

  struct list_elem elem; 
};


#ifdef FS_DEBUG
#define FS_IN lock_fs();
#define FS_OUT unlock_fs();
#else
#define FS_IN 
#define FS_OUT
#endif

void fsaccess_init (void);

bool create_file(const char *file, unsigned length);
bool create_directory(const char *dirpath);
bool change_directory(const char *dirpath);
bool remove_file_or_dir(const char *file);

struct file_descriptor * get_file_descriptor (int fd_num);
int open_file_or_dir(const char *filename);
int filelength_open_file (int fd_num);
int read_open_file(int fd_num, void *buffer, unsigned length);
int write_open_file (int fd_num, void *buffer, unsigned length);
void seek_open_file (int fd_num, unsigned position);
unsigned tell_open_file (int fd_num);
int memory_map_file (int fd_num, void *start_page);
void memory_unmap_file (int map_id);
void close_open_file_or_dir (int fd_num);
void close_all_files_and_dir(void);
bool read_directory (int fd, char *name);
bool is_directory (int fd);
int fd_inode_number (int fd);
bool is_dir_open_fd_global (struct dir *dir);
bool is_dir_cwd_global (struct dir *dir);

void lock_fs (void);
void unlock_fs(void);
#endif