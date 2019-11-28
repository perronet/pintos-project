#include "fsaccess.h"
#include "lib/stdio.h"
#include "kernel/stdio.h"
#include "threads/malloc.h"
#include "filesys/file.h"
#include "devices/input.h"
#include "filesys/filesys.h"

#define FIRST_VALID_FILE_DESCRIPTOR 2;

static struct list open_files;

void
fsaccess_init (void)
{
  lock_init (&files_lock);
  list_init (&open_files);
  fd_count = FIRST_VALID_FILE_DESCRIPTOR;
}

bool 
create_file(const char *file, unsigned length)
{
  bool result = false;
  if (is_valid_address_of_thread (thread_current (), file))
    {
      lock_acquire (&files_lock);
      result = filesys_create(file, length);
      lock_release (&files_lock);
    }
  
  return result;
}

bool 
remove_file(const char *file)
{
  bool result = false;
  if (is_valid_address_of_thread (thread_current (), file))
    {    
      lock_acquire (&files_lock);
      result = filesys_remove(file);
      lock_release (&files_lock);
    }

  return result;
}

struct file_descriptor *
get_file_descriptor (int fd_num)
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
open_file(const char *filename)
{
  struct file_descriptor *fd = malloc(sizeof(struct file_descriptor));  
  
  lock_acquire (&files_lock);
  struct file * f = filesys_open (filename);

  if(f == NULL)
  {
    lock_release (&files_lock);
    if(fd != NULL)
      free(fd);

    return -1;
  }
  else
  {
    fd->fd_num = fd_count;
    fd->open_file = f;
    fd->owner = thread_current ()->tid;
    list_push_front (&open_files, &fd->elem);
    fd_count ++;

    lock_release (&files_lock);
    return fd->fd_num;
  }
}

int filelength_open_file (int fd_num)
{
  int result = -1;

  lock_acquire (&files_lock);
  struct file_descriptor *fd = get_file_descriptor (fd_num);
  if (fd != NULL)
    result = file_length (fd->open_file);
  lock_release (&files_lock);

  return result;
}

int
read_open_file(int fd_num, void *buffer, unsigned length)
{
  int result = 0;

  if (fd_num == STDIN_FILENO)
    {
      char * start = buffer;
      char * end = start + length;
      char c;

      lock_acquire (&files_lock);
      while(start < end && (c = input_getc()) != 0)
      {
        *start = c;
        start++;
        result++;
      }
      lock_release(&files_lock); 

      *start = 0;
    }
  else if (fd_num == STDOUT_FILENO)
    result = -1;
  else //it is an actual file descriptor
    {
      lock_acquire (&files_lock); 
      struct file_descriptor *fd = get_file_descriptor (fd_num);
      if (fd != NULL)
        result = file_read (fd->open_file, buffer, length);
      else
        result = -1;

      lock_release(&files_lock);
    }

  return result;
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
      putbuf (buffer, length); //#TODO check for too long buffers, break them down.
      lock_release(&files_lock); 
    }
  else //it is an actual file descriptor
    {
      lock_acquire (&files_lock); 
      struct file_descriptor *fd = get_file_descriptor (fd_num);
      if (fd != NULL)
        result = file_write (fd->open_file, buffer, length);
      else
        result = -1;
      lock_release(&files_lock); 
    }

  return result;
}

/* Sets the current position in FILE to NEW_POS bytes from the
   start of the file. */  
void
seek_open_file (int fd_num, unsigned position)
{
  lock_acquire (&files_lock);
  struct file_descriptor *fd = get_file_descriptor (fd_num);
  if (fd != NULL)
    file_seek (fd->open_file, position);
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
  struct file_descriptor *fd = get_file_descriptor (fd_num);
  if (fd != NULL)
    result = file_tell (fd->open_file);
  lock_release (&files_lock);

  return result;
}

void
close_open_file (int fd_num)
{
  lock_acquire (&files_lock); 
  struct file_descriptor *fd = get_file_descriptor (fd_num);
  if (fd != NULL && fd->owner == thread_current ()->tid)
  {
    file_close(fd->open_file);
    list_remove (&fd->elem);      
    free(fd);
  }
  lock_release (&files_lock);
}

void 
close_all_files()
{
  lock_acquire (&files_lock);

  struct list_elem *e;
  e = list_tail (&open_files);
  while ((e = list_prev (e)) != list_head (&open_files)) 
    {
      struct file_descriptor *fd;
      fd = list_entry (e, struct file_descriptor, elem);
      if (fd->owner == thread_current ()->tid)
        {
          file_close(fd->open_file);
          e = list_next(e);
          list_remove (&fd->elem);      
          free(fd);
        }
    }

  lock_release (&files_lock);

}
