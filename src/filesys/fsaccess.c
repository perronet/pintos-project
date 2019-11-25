#include "fsaccess.h"
#include "lib/stdio.h"
#include "kernel/stdio.h"
#include "filesys/file.h"

void
fsaccess_init (void)
{
  lock_init (&files_lock);
  list_init (&open_files);
}

struct file_descriptor *
try_get_open_file (int fd_num)
{
  struct list_elem *e;
  e = list_tail (&open_files);
  while ((e = list_prev (e)) != list_head (&open_files)) 
    {
      struct file_descriptor *fd;
      fd = list_entry (e, struct file_descriptor, elem);
      if (fd->fd_num == fd_num)
          return fd;
    }
  return NULL;
}

int 
write_open_file (int fd_num, void *buffer, unsigned length)
{
  int result = 0;

  struct thread * current = thread_current ();
  if (!is_valid_address_range_of_thread (current, buffer, buffer + length))
    result = -1;
  
  if (fd_num == STDIN_FILENO)
      result = -1;
  else if (fd_num == STDOUT_FILENO)
    {
      lock_acquire (&files_lock); 
      putbuf (buffer, length);
      lock_release(&files_lock); 
    }
  else //it an actual file descriptor
    {
      lock_acquire (&files_lock); 
      struct file_descriptor *fd = try_get_open_file (fd_num);
      if (fd != NULL)
        result = file_write (fd->file_struct, buffer, length);
      lock_release(&files_lock); 
    }

  return result;
}

/* Sets the current position in FILE to NEW_POS bytes from the
   start of the file. */  
void
try_seek_open_file (int fd_num, unsigned position)
{
  lock_acquire (&files_lock);
  struct file_descriptor *fd = try_get_open_file (fd_num);
  if (fd != NULL)
    file_seek (fd->file_struct, position);
  lock_release (&files_lock);
}

/* Returns the current position in FILE as a byte offset from the
   start of the file. Returns zero also in case the file is not
   open. */
unsigned
tell_open_file (int fd_num)
{
  int result = 0;

  lock_acquire (&files_lock);
  struct file_descriptor *fd = try_get_open_file (fd_num);
  if (fd != NULL)
    result = file_tell (fd->file_struct);
  lock_release (&files_lock);

  return result;
}

void
try_close_open_file (int fd_num)
{
  lock_acquire (&files_lock); 
  struct file_descriptor *fd = try_get_open_file (fd_num);
  if (fd != NULL)
      list_remove (&fd->elem);
  lock_release (&files_lock);
}