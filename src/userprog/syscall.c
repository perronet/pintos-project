#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "syscall.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "filesys/fsaccess.h"
#include "userprog/process.h"
#include "devices/shutdown.h"

static void syscall_handler (struct intr_frame *);
static void halt (void);
static void exit (int status);
static pid_t exec (const char *file);
static int wait (pid_t pid);
static bool create (const char *file, unsigned initial_size);
static bool remove (const char *file);
static int open (const char *file);
static int filesize (int fd);
static int read (int fd, void *buffer, unsigned length, void *esp);
static int write (int fd, const void *buffer, unsigned length);
static void seek (int fd, unsigned position);
static unsigned tell (int fd);
static void close (int fd);
static int mmap (int fd, void *page);
static void munmap (int map_id);
static bool chdir (const char *dir);
static bool mkdir (const char *dir);
static bool readdir (int fd, char *name);
static bool isdir (int fd);
static int inumber (int fd);

#define CHECK_PTR(esp, wants_to_write) \
{\
  if (!is_valid_address_of_thread (thread_current (), esp, wants_to_write, 0))\
    exit (-1);\
}

#define CHECK_PTR_RANGE(start, end, wants_to_write, esp) \
{\
  if (!is_valid_address_range_of_thread (thread_current (), start, end, wants_to_write, esp))\
    exit(-1);\
}

#define GET_PARAM(esp,type)\
({\
    CHECK_PTR(esp, false);\
    type ret = *(type*)esp;\
    esp += sizeof(type*);\
    ret;\
})

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  fsaccess_init();
}

static void
syscall_handler (struct intr_frame *f) 
{
  CHECK_PTR(f->esp, false);
  
  void *esp = f->esp;
  int syscall_id = *(int *)esp;
  esp += sizeof(int);

  int fd, map_id, status;
  pid_t pid;
  void *buffer;
  const void *buffer_cnst;
  const char *file, *dir;
  char *name;
  unsigned size, position, initial_size;
  switch (syscall_id)
  {
    case SYS_HALT:
      halt ();
    break;
    case SYS_EXIT:
      status = GET_PARAM(esp, int);

      exit (status);
    break;
    case SYS_EXEC:
      file = GET_PARAM(esp, char *);

      f->eax = exec (file);
    break;
    case SYS_WAIT:
      pid = GET_PARAM(esp, pid_t);

      f->eax = wait (pid);
    break;
    case SYS_CREATE:
      file = GET_PARAM(esp, char *);
      initial_size = GET_PARAM(esp, unsigned);

      f->eax = create (file, initial_size);
    break;
    case SYS_REMOVE:
      file = GET_PARAM(esp, char *);

      f->eax = remove (file);
    break;
    case SYS_OPEN:
      file = GET_PARAM(esp, char *);

      f->eax = open (file);
    break;
    case SYS_FILESIZE:
      fd = GET_PARAM(esp, int);

      f->eax = filesize (fd);
    break;
    case SYS_READ:
      fd = GET_PARAM(esp, int);
      buffer = GET_PARAM(esp, void *);
      size = GET_PARAM(esp, unsigned);

      f->eax = read (fd, buffer, size, f->esp); 
    break;
    case SYS_WRITE:
      fd = GET_PARAM(esp, int);
      buffer_cnst = GET_PARAM(esp, void *);
      size = GET_PARAM(esp, unsigned);

      f->eax = write (fd, buffer_cnst, size);
    break;
    case SYS_SEEK:
      fd = GET_PARAM(esp, int);
      position = GET_PARAM(esp, unsigned);

      seek (fd, position);
    break;
    case SYS_TELL:
      fd = GET_PARAM(esp, int);

      f->eax = tell (fd);
    break;
    case SYS_CLOSE:
      fd = GET_PARAM(esp, int);

      close (fd);
    break;

    // Assignment 3 and 4
    case SYS_MMAP:
      fd = GET_PARAM(esp, int);
      buffer = GET_PARAM(esp, void *);

      f->eax = mmap (fd, buffer);
    break;
    case SYS_MUNMAP:
      map_id = GET_PARAM(esp, int);

      munmap(map_id);
    break;
    case SYS_CHDIR:
      dir = GET_PARAM(esp, char *);

      f->eax = chdir (dir);
    break;
    case SYS_MKDIR:
      dir = GET_PARAM(esp, char *);

      f->eax = mkdir (dir);
    break;
    case SYS_READDIR:
      fd = GET_PARAM(esp, int);
      name = GET_PARAM(esp, char *);

      f->eax = readdir (fd, name);
    break;
    case SYS_ISDIR:
      fd = GET_PARAM(esp, int);

      f->eax = isdir (fd);
    break;
    case SYS_INUMBER:
      fd = GET_PARAM(esp, int);

      f->eax = inumber (fd);
    break;
  }
}

static void halt ()
{
  shutdown_power_off ();
}

static void exit (int status)
{
  thread_exit_with_status (status);
}

static pid_t exec (const char *file)
{
  CHECK_PTR(file, false);

  struct thread *cur = thread_current ();
  cur->child_born_status = 0;
  tid_t tid = process_execute (file);

  if(tid != TID_ERROR)
    sema_down (&cur->child_sema);
  if (cur->child_born_status == -1)
    return -1;
  else
    return tid;
}

static int wait (pid_t pid)
{
  return process_wait (pid);
}

static bool create (const char *file, unsigned initial_size)
{
  CHECK_PTR(file, false);
  return create_file(file, initial_size);
}

static bool remove (const char *file)
{
  CHECK_PTR(file, false);
  return remove_file_or_dir(file);
}

static int open (const char *file)
{
  CHECK_PTR(file, false);
  return open_file_or_dir(file);
}

static int filesize (int fd)
{
  return filelength_open_file (fd);
}

static int read (int fd, void *buffer, unsigned length, void *esp)
{
  CHECK_PTR_RANGE(buffer, buffer + length, true, esp);
  return read_open_file(fd, buffer, length);
}

/* Writes to the given fd. 
   NOTE: remember, "\n" is length 1 but also "a\n" is length 1
   */
static int write (int fd, const void *buffer, unsigned length)
{
  void *buf = (void *)buffer;
  CHECK_PTR_RANGE(buf, buf + length, false, 0);
  return write_open_file (fd, buf, length);
}

static void seek (int fd, unsigned position)
{
  seek_open_file (fd, position);
}

static unsigned tell (int fd)
{
  return tell_open_file (fd);
}

static void close (int fd)
{
  close_open_file_or_dir (fd);
}

static int mmap (int fd, void *page)
{
  return memory_map_file (fd, page);
}

static void munmap (int map_id)
{
  memory_unmap_file (map_id);
}

static bool chdir (const char *dir)
{
  CHECK_PTR(dir, false);

  return change_directory (dir);
}

static bool mkdir (const char *dir)
{
  CHECK_PTR(dir, false);

  return create_directory (dir);
}

static bool readdir (int fd, char *name)
{
  CHECK_PTR(name, true);

  return read_directory (fd, name);
}

static bool isdir (int fd)
{
  return is_directory (fd);
}

static int inumber (int fd)
{
  return fd_inode_number (fd);
}