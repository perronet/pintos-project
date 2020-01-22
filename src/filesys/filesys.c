#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "cache.h"

/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) 
{
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");

  bc_init ();
  inode_init ();
  free_map_init ();

  if (format) 
    do_format ();

  free_map_open ();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void) 
{
  bc_flush_all();
  free_map_close ();
}

/* Creates a file or directory named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. 
   If INITIAL_SIZE is negative, then we create a 
   directory. */
bool
filesys_create (const char *path, off_t initial_size) 
{
  const char *last_entry = get_path_last_entry (path);
  block_sector_t inode_sector = 0;
  block_sector_t parent_dir_sector;
  bool success;

  struct dir *parent_dir = get_parent_directory (path);
  if (parent_dir == NULL)
    return false;
  parent_dir_sector = inode_get_inumber (dir_get_inode (parent_dir));

  success = free_map_allocate (1, &inode_sector);
  if (initial_size >= 0)
  {
    success = success && inode_create (inode_sector, initial_size, parent_dir_sector, false); // Create file
    success = success && dir_add (parent_dir, last_entry, inode_sector, false);
  }
  else
  {                  
    success = success && dir_create (inode_sector, DIR_INITIAL_SIZE, parent_dir_sector); // Create directory
    success = success && dir_add (parent_dir, last_entry, inode_sector, true);
  }

  if (!success && inode_sector != 0) 
    free_map_release (inode_sector, 1);

  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *filepath)
{
  struct inode *inode = NULL;
  const char *last_entry = get_path_last_entry (filepath);
  struct dir *parent_dir = get_parent_directory (filepath);

  if (parent_dir != NULL)
    dir_lookup_entry (parent_dir, last_entry, &inode, false);

  return file_open (inode);
}

/* Deletes the file or directory named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *path) 
{
  if (!strcmp (path, "/"))
    return false;

  const char *last_entry = get_path_last_entry (path);
  struct dir *parent_dir = get_parent_directory (path);

  bool success = parent_dir != NULL && dir_remove (parent_dir, last_entry);

  return success;
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16, ROOT_DIR_SECTOR))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}
