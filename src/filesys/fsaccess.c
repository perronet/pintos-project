#include "fsaccess.h"
#include "lib/stdio.h"
#include "kernel/stdio.h"
#include "threads/malloc.h"
#include "filesys/file.h"
#include "devices/input.h"
#include "filesys/filesys.h"
#include "threads/vaddr.h"
#include "vm/page.h"

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
  if (is_valid_address_of_thread (thread_current (), file, false, 0))
    {
      lock_fs ();
      result = filesys_create(file, length);
      unlock_fs ();
    }
  
  return result;
}

bool 
remove_file(const char *file)
{
  bool result = false;
  if (is_valid_address_of_thread (thread_current (), file, false, 0))
    {    
      lock_fs ();
      result = filesys_remove(file);
      unlock_fs ();
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
      if (fd->fd_num == fd_num && fd->owner == thread_current()->tid)
          return fd;
    }

  return NULL;
}

int
open_file(const char *filename)
{
  struct file_descriptor *fd = malloc(sizeof(struct file_descriptor));  
  
  lock_fs ();
  struct file *f = filesys_open (filename);

  if(f == NULL)
  {
    unlock_fs ();
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

    unlock_fs ();
    return fd->fd_num;
  }
}

int filelength_open_file (int fd_num)
{
  int result = -1;

  lock_fs ();
  struct file_descriptor *fd = get_file_descriptor (fd_num);
  if (fd != NULL)
    result = file_length (fd->open_file);
  unlock_fs ();

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

      lock_fs ();
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
      lock_fs (); 
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
  if (!is_valid_address_range_of_thread (current, buffer, buffer + length, false, 0))
    result = -1;
  
  if (fd_num == STDIN_FILENO)
      result = -1;
  else if (fd_num == STDOUT_FILENO)
    {
      lock_fs (); 
      putbuf (buffer, length); //#TODO check for too long buffers, break them down.
      lock_release(&files_lock); 
    }
  else //it is an actual file descriptor
    {
      lock_fs (); 
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
  lock_fs ();
  struct file_descriptor *fd = get_file_descriptor (fd_num);
  if (fd != NULL)
    file_seek (fd->open_file, position);
  unlock_fs ();
}

/* Returns the current position in FILE as a byte offset from the
   start of the file. Returns zero also in case the file is not
   open. */
unsigned
tell_open_file (int fd_num)
{
  int result = 0;

  lock_fs ();
  struct file_descriptor *fd = get_file_descriptor (fd_num);
  if (fd != NULL)
    result = file_tell (fd->open_file);
  unlock_fs ();

  return result;
}

int 
memory_map_file (int fd_num, void *start_page)
{
  struct file_descriptor *fd = get_file_descriptor (fd_num);

  if (fd == NULL || fd_num == 0 || fd_num == 1 || 
    start_page == 0 || !is_start_of_page (start_page)){
    return -1;
  }

  struct file *f = fd->open_file;
  ASSERT (f != NULL);

  lock_fs ();
  struct file *rf = file_reopen(f);
  unlock_fs ();

  int map_id = pt_suppl_handle_mmap (rf, start_page);
  return map_id;
}

void 
memory_unmap_file (int map_id)
{
  struct thread *current = thread_current ();
  lock_acquire (&current->pt_suppl_lock);
  pt_suppl_handle_unmap (map_id);
  lock_release (&current->pt_suppl_lock);
}

void
close_open_file (int fd_num)
{
  lock_fs (); 
  struct file_descriptor *fd = get_file_descriptor (fd_num);
  if (fd != NULL && fd->owner == thread_current ()->tid)
  {
    file_close(fd->open_file);
    list_remove (&fd->elem);      
    free(fd);
  }
  unlock_fs ();
}

void 
close_all_files()
{
  lock_fs ();

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

  unlock_fs ();

  unmap_all();
}

void lock_fs ()
{
  lock_acquire (&files_lock);
}

void unlock_fs()
{
  lock_release (&files_lock);
}