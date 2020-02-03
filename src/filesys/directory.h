#ifndef FILESYS_DIRECTORY_H
#define FILESYS_DIRECTORY_H

#include <stdbool.h>
#include <stddef.h>
#include <filesys/off_t.h>
#include "devices/block.h"
#include "filesys/inode.h"

/* Maximum length of a file name component.
   This is the traditional UNIX maximum length.
   After directories are implemented, this maximum length may be
   retained, but much longer full path names must be allowed. */
#define NAME_MAX 63
#define DIR_PATH_MAX 600
#define DIR_INITIAL_SIZE 10

/* An open directory. */
struct dir 
  {
    struct inode *inode;                /* Backing store. */
    struct lock dir_lock;               /* Used to synch dir modifications */
    off_t pos;                          /* Current position. */
  };

/* A single directory entry. */
struct dir_entry 
  {
    block_sector_t inode_sector;        /* Sector number of header. */
    char name[NAME_MAX + 1];            /* Null terminated file or directory name. */
    bool in_use;                        /* In use or free? */
    bool is_dir;                        /* Is a directory or a file? */
  };

/* Opening and closing directories. */
bool dir_create (block_sector_t sector, size_t entry_cnt, block_sector_t parent);
struct dir *dir_open (struct inode *);
struct dir *dir_open_root (void);
struct dir *dir_reopen (struct dir *);
void dir_close (struct dir *);
struct inode *dir_get_inode (struct dir *);

/* Reading and writing. */
bool dir_lookup_entry (const struct dir *, const char *name, struct inode **, bool is_dir);
struct inode * dir_path_lookup (const char *path_str);
bool dir_add (struct dir *, const char *name, block_sector_t, bool is_dir);
bool dir_remove (struct dir *, const char *name);
bool dir_readdir (struct dir *, char name[NAME_MAX + 1]);
bool path_str_wellformed (const char *path_str);
const char * get_path_last_entry (const char *path_str);
bool get_path_entry (const char *path_str, int n, char *buffer);
int get_path_entry_cnt (const char *path_str);
struct dir* get_parent_directory (const char *path_str);
struct dir* get_curr_working_dir(void);
bool path_is_dir (const char *path_str);
bool dir_is_empty (struct dir *dir);

#endif /* filesys/directory.h */
