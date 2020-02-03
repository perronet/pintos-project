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
create_file(const char *filepath, unsigned length) 
{
  bool result = false;
  if (is_valid_address_of_thread (thread_current (), filepath, false, 0) && strlen (filepath))
    {
      lock_fs ();
      result = filesys_create(filepath, length);
      unlock_fs ();
    }
  
  return result;
}

bool
create_directory(const char *dirpath) 
{
  bool result = false;
  if (is_valid_address_of_thread (thread_current (), dirpath, false, 0) && strlen (dirpath))
    {
      result = filesys_create (dirpath, -1);
    }
  
  return result;
}

bool
change_directory(const char *dirpath)
{
  struct dir *new_dir;
  struct dir *old_dir;

  if (dirpath == NULL)
    return false;

  lock_fs ();

  if ((new_dir = dir_open (dir_path_lookup (dirpath))) == NULL)
  {
    unlock_fs ();
    return false;
  }

  old_dir = get_curr_working_dir ();
  old_dir->inode->cwd_cnt--;
  dir_close (old_dir);
  new_dir->inode->cwd_cnt++;
  thread_current ()->curr_dir = new_dir;

  unlock_fs ();

  return true;
}

bool 
remove_file_or_dir(const char *path)
{
  bool result = false;
  if (is_valid_address_of_thread (thread_current (), path, false, 0))
    {    
      if (path_is_dir (path))
      { 
      /* Remove directory only if it's empty and it's not open
         with a file descriptor or a cwd of any process 
         (it's enough to check that there are zero openers) */
        struct dir *dir = dir_open (dir_path_lookup (path));
        if (dir_is_empty (dir) && !is_dir_open_fd_global (dir) && !is_dir_cwd_global (dir))
          result = filesys_remove (path);
      }
      else
      {
        result = filesys_remove (path);
      }
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

/* Open directory with file descriptor */
int
open_file_or_dir(const char *path) 
{
  ASSERT (path != NULL);
  struct file_descriptor *fd = malloc(sizeof(struct file_descriptor));
  struct dir *dir = (void *)0x1;
  struct file *f = (void *)0x1;
  lock_fs ();

  bool is_dir = path_is_dir (path);
  if (is_dir)
    dir = dir_open (dir_path_lookup (path));
  else
    f = filesys_open (path);

  if(f == NULL || dir == NULL)
  {
    unlock_fs ();
    if(fd != NULL)
      free(fd);

    return -1;
  }
  else
  {
    if (is_dir)
    {
      fd->open_file = NULL;
      fd->open_dir = dir;
      dir->inode->open_fd_cnt++;
    }
    else
    {
      fd->open_dir = NULL;
      fd->open_file = f;  
    }

    fd->fd_num = fd_count;
    fd->owner = thread_current ()->tid;
    fd->is_dir = is_dir;
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
read_open_file (int fd_num, void *buffer, unsigned length)
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
      unlock_fs (); 

      *start = 0;
    }
  else if (fd_num == STDOUT_FILENO)
    result = -1;
  else //it is an actual file descriptor
    {
      lock_fs (); 
      struct file_descriptor *fd = get_file_descriptor (fd_num);
      unlock_fs (); 

      FS_IN;
      if (fd != NULL && !fd->is_dir)
        result = file_read (fd->open_file, buffer, length);
      else
        result = -1;
      FS_OUT;
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
      unlock_fs (); 
    }
  else //it is an actual file descriptor
    {
      lock_fs (); 
      struct file_descriptor *fd = get_file_descriptor (fd_num);
      unlock_fs ();

      FS_IN;
      if (fd != NULL && !fd->is_dir)
        result = file_write (fd->open_file, buffer, length);
      else
        result = -1;
      FS_OUT;
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
close_open_file_or_dir (int fd_num)
{
  lock_fs (); 
  struct file_descriptor *fd = get_file_descriptor (fd_num);
  if (fd != NULL && fd->owner == thread_current ()->tid)
  {
    if (fd->is_dir)
    {
      fd->open_dir->inode->open_fd_cnt--;
    }
    else
    {
      file_close(fd->open_file);
    }
    
    list_remove (&fd->elem);      
    free(fd);
  }
  unlock_fs ();
}

void 
close_all_files_and_dir ()
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
          if (fd->is_dir)
          {
            fd->open_dir->inode->open_fd_cnt--;
          }
          else
          {
            file_close(fd->open_file);
          }
          e = list_next(e);
          list_remove (&fd->elem);      
          free(fd);
        }
    }

  unlock_fs ();

  unmap_all();
}

bool 
read_directory (int fd, char *name)
{
  struct file_descriptor *f = get_file_descriptor (fd);

  if (f == NULL || !f->is_dir || 
    !is_valid_address_range_of_thread (thread_current (), name, name + NAME_MAX + 1, true, 0))
    return false;

  return dir_readdir (f->open_dir, name);
}

bool
is_directory (int fd)
{
  struct file_descriptor *f = get_file_descriptor (fd);

  if (f != NULL)
    return f->is_dir;
  else
    return false;
}

int
fd_inode_number (int fd)
{
  struct file_descriptor *f = get_file_descriptor (fd);

  if (f == NULL)
    return -1;

  if (f->open_file != NULL)
    return inode_get_inumber (f->open_file->inode);
  else if (f->open_dir != NULL)
    return inode_get_inumber (f->open_dir->inode);
  else
    return -1;
}

bool is_dir_open_fd_global (struct dir *dir)
{
  ASSERT (dir != NULL);
  return (dir->inode->open_fd_cnt > 0);
}

bool 
is_dir_cwd_global (struct dir *dir)
{
  ASSERT (dir != NULL);
  return (dir->inode->cwd_cnt > 0);
}

void lock_fs ()
{
  lock_acquire (&files_lock);
}

void unlock_fs()
{
  lock_release (&files_lock);
}