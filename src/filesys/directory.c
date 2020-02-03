#include "filesys/directory.h"
#include <stdio.h>
#include <string.h>
#include <list.h>
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "threads/malloc.h"
#include "threads/thread.h"

#define DIR_CHECK (is_dir ? (e.is_dir) : (true))

/* Creates a directory with space for ENTRY_CNT entries in the
   given SECTOR.  Returns true if successful, false on failure. */
bool
dir_create (block_sector_t sector, size_t initial_entry_cnt, block_sector_t parent)
{
  return inode_create (sector, initial_entry_cnt * sizeof (struct dir_entry), parent, false);
}

/* Opens and returns the directory for the given INODE, of which
   it takes ownership.  Returns a null pointer on failure. */
struct dir *
dir_open (struct inode *inode) 
{
  struct dir *dir = calloc (1, sizeof *dir);
  if (inode != NULL && dir != NULL)
    {
      dir->inode = inode;
      lock_init (&dir->dir_lock);
      dir->pos = 0;
      return dir;
    }
  else
    {
      inode_close (inode);
      free (dir);
      return NULL; 
    }
}

/* Opens the root directory and returns a directory for it.
   Return true if successful, false on failure. */
struct dir *
dir_open_root (void)
{
  return dir_open (inode_open (ROOT_DIR_SECTOR));
}

/* Opens and returns a new directory for the same inode as DIR.
   Returns a null pointer on failure. */
struct dir *
dir_reopen (struct dir *dir) 
{
  return dir_open (inode_reopen (dir->inode));
}

/* Destroys DIR and frees associated resources. */
void
dir_close (struct dir *dir) 
{
  if (dir != NULL)
    {
      inode_close (dir->inode);
      free (dir);
    }
}

/* Returns the inode encapsulated by DIR. */
struct inode *
dir_get_inode (struct dir *dir) 
{
  return dir->inode;
}

/* Searches DIR for a file with the given NAME.
   If successful, returns true, sets *EP to the directory entry
   if EP is non-null, and sets *OFSP to the byte offset of the
   directory entry if OFSP is non-null.
   otherwise, returns false and ignores EP and OFSP. */
static bool
lookup (const struct dir *dir, const char *name,
        struct dir_entry *ep, off_t *ofsp) 
{
  struct dir_entry e;
  size_t ofs;
  
  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e) 
    if (e.in_use && !strcmp (name, e.name)) 
      {
        if (ep != NULL)
          *ep = e;
        if (ofsp != NULL)
          *ofsp = ofs;
        return true;
      }
  return false;
}

/* Searches DIR for a file or folder with the given NAME
   and returns true if one exists, false otherwise.
   On success, sets *INODE to an inode for the file, otherwise to
   a null pointer.  The caller must close *INODE. */
bool
dir_lookup_entry (const struct dir *dir, const char *name,
            struct inode **inode, bool is_dir) 
{
  struct dir_entry e;

  if (dir != NULL && name != NULL && lookup (dir, name, &e, NULL) && DIR_CHECK)
    *inode = inode_open (e.inode_sector);
  else
    *inode = NULL;

  return *inode != NULL && DIR_CHECK;
}

/* Caller must close the returned inode */
struct inode *
dir_path_lookup (const char *path_str)
{
  if (path_str == NULL || !path_str_wellformed (path_str))
    return NULL;

  struct dir *working_dir = NULL;
  struct dir *prev_working_dir = NULL;
  struct inode *inode = NULL;
  block_sector_t working_dir_sector;
  char dir_entry_name[NAME_MAX + 1];
  const char *target = get_path_last_entry (path_str);
  int last_entry_idx = get_path_entry_cnt (path_str) - 1;

  if (path_str[0] == '/')
  { // Absolute path
    if (path_str[1] == '\0')
      return inode_open (ROOT_DIR_SECTOR);

    working_dir = dir_open_root ();
  }
  else
  { // Relative path
    working_dir = get_curr_working_dir ();
  }

  for (int i = 0; strcmp (dir_entry_name, target) || i <= last_entry_idx; i++)
  {
    get_path_entry (path_str, i, dir_entry_name);

    if (!strcmp (dir_entry_name, "."))
    {
      inode = dir_get_inode (working_dir);
      continue;
    }
    else if (!strcmp (dir_entry_name, ".."))
    {
      working_dir_sector = inode_get_parent (dir_get_inode (working_dir));
      working_dir = dir_open (inode_open (working_dir_sector));
      inode = dir_get_inode (working_dir);
    }
    else
    {
      if (!dir_lookup_entry (working_dir, dir_entry_name, &inode, true))
        return NULL;
      working_dir = dir_open (inode);
    }
    dir_close (prev_working_dir);
    prev_working_dir = working_dir;
  }

  return inode;
}

/* Adds a file or directory named NAME to DIR, which must not 
   already contain a file by that name.
   The file's inode is in sector INODE_SECTOR.
   Returns true if successful, false on failure.
   Fails if NAME is invalid (i.e. too long) or a disk or memory
   error occurs. */
bool
dir_add (struct dir *dir, const char *name, block_sector_t inode_sector, bool is_dir)
{
  lock_acquire (&dir->dir_lock);
  struct dir_entry e;
  off_t ofs;
  bool success = false;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  /* Check NAME for validity. */
  if (*name == '\0' || strlen (name) > NAME_MAX)
    return false;

  /* Check that NAME is not in use. */
  if (lookup (dir, name, NULL, NULL))
    goto done;

  /* Set OFS to offset of free slot.
     If there are no free slots, then it will be set to the
     current end-of-file.
     
     inode_read_at() will only return a short read at end of file.
     Otherwise, we'd need to verify that we didn't get a short
     read due to something intermittent such as low memory. */
  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e) 
    if (!e.in_use)
      break;

  /* Write slot. */
  strlcpy (e.name, name, sizeof e.name);
  e.inode_sector = inode_sector;
  e.in_use = true;
  e.is_dir = is_dir;

  success = inode_write_at (dir->inode, &e, sizeof e, ofs) == sizeof e;

 done:
  lock_release (&dir->dir_lock);
  return success;
}

/* Removes any entry for NAME in DIR. If NAME is a directory,
   remove it if it's empty.
   Returns true if successful, false on failure,
   which occurs only if there is no file with the given NAME. */
bool
dir_remove (struct dir *dir, const char *name) 
{
  lock_acquire (&dir->dir_lock);
  struct dir_entry e;
  struct inode *inode = NULL;
  struct dir *dir_to_remove = NULL;
  bool success = false;
  off_t ofs;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  /* Find directory entry. */
  if (!lookup (dir, name, &e, &ofs))
    goto done;

  /* Open inode. */
  inode = inode_open (e.inode_sector);
  if (inode == NULL)
    goto done;

  /* Remove directory only if it's empty */
  if (e.is_dir)
  {
    dir_to_remove = dir_open (inode);
    if (!dir_is_empty (dir_to_remove))
      goto done;
  }

  /* Erase directory entry. */
  e.in_use = false;
  if (inode_write_at (dir->inode, &e, sizeof e, ofs) != sizeof e) 
    goto done;

  /* Remove inode. */
  inode_remove (inode);
  success = true;

 done:
  dir_close (dir_to_remove);
  inode_close (inode);
  lock_release (&dir->dir_lock);
  return success;
}

/* Reads the next directory entry in DIR and stores the name in
   NAME.  Returns true if successful, false if the directory
   contains no more entries. */
bool
dir_readdir (struct dir *dir, char name[NAME_MAX + 1])
{
  struct dir_entry e;

  while (inode_read_at (dir->inode, &e, sizeof e, dir->pos) == sizeof e) 
    {
      dir->pos += sizeof e;
      if (e.in_use)
        {
          strlcpy (name, e.name, NAME_MAX + 1);
          return true;
        } 
    }
  return false;
}

bool
path_str_wellformed (const char *path_str UNUSED) //TODO
{
  return true;
}

const char *
get_path_last_entry (const char *path_str) 
{
  char *last = strrchr (path_str, '/');
  if (last != NULL && strlen (path_str) == 1)
    last--;
  return last != NULL ? ++last : path_str;
}

bool
get_path_entry (const char *path_str, int n, char *buffer)
{
  char *save_ptr, *token, *cpy = calloc (strlen (path_str)+1, sizeof (char));
  strlcpy (cpy, path_str, strlen (path_str)+1);
  int i = 0;

  token = strtok_r (cpy, "/", &save_ptr);
  while (token != NULL && i < n)
  {
    token = strtok_r (NULL, "/", &save_ptr);
    i++;
  }

  if (token == NULL)
  {
    free (cpy);
    return false;
  }
  else
  {
    strlcpy (buffer, token, strlen (token)+1);
    free (cpy);
    return true;
  }
}

int
get_path_entry_cnt (const char *path_str)
{
  char *save_ptr, *token, *cpy = calloc (strlen (path_str)+1, sizeof (char));
  strlcpy (cpy, path_str, strlen (path_str)+1);
  int cnt = 0;

  token = strtok_r (cpy, "/", &save_ptr);
  while (token != NULL)
  {
    token = strtok_r (NULL, "/", &save_ptr);
    cnt++;
  }

  free (cpy);
  return cnt;
}

/* Caller must close the returned open directory.
   The parent may not be accessible as the inode for the 
   child may not exist yet: it's important to use only
   the path to find the parent directory */
struct dir*
get_parent_directory (const char *path_str)
{
  struct dir* dir;
  char buffer[DIR_PATH_MAX + 1];
  ASSERT (path_str != NULL);
  if (strrchr (path_str, '/') == NULL) 
  { // No slashes: parent directory is the working directory
    dir = get_curr_working_dir();
  } // Only one entry: parent directory is root //TODO is "a/" illegal?
  else if (!get_path_entry (path_str, 1, buffer))
  {
    dir = dir_open_root ();
  }
  else
  { // More than one entry

    // Cut last entry
    strlcpy (buffer, path_str, strlen (path_str)+1);
    char *last = strrchr (buffer, '/');
    ASSERT (last != NULL);
    *last = '\0';

    // Lookup
    dir = dir_open (dir_path_lookup (buffer));
  }

  return dir;
}

/* Caller must close the returned open directory */
struct dir* get_curr_working_dir()
{
  struct dir* working_dir = thread_current ()->curr_dir;
  if (working_dir == NULL)
    return dir_open_root ();
  else
    return working_dir;
}

bool
path_is_dir (const char *path_str)
{
  struct dir *parent_dir = get_parent_directory (path_str); 
  const char *target = get_path_last_entry (path_str);
  struct inode *entry_inode = NULL;

  if (dir_lookup_entry (parent_dir, target, &entry_inode, true)
      || !strcmp (target, "/") || !strcmp (target, ".") || !strcmp (target, ".."))
    return true;
  
  return false;
}

bool
dir_is_empty (struct dir *dir)
{
  struct dir_entry e;
  size_t ofs;
  ASSERT (dir != NULL);

  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e; ofs += sizeof e)
    if (e.in_use)
      return false;

  return true;
}