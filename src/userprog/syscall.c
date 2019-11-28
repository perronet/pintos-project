#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "syscall.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "filesys/fsaccess.h"
#include "userprog/process.h"
#include "devices/shutdown.h"

//TODO move these to syscall.h
static void syscall_handler (struct intr_frame *);
static void halt (void);
static void exit (int status);
static pid_t exec (const char *file);
static int wait (pid_t pid);
static bool create (const char *file, unsigned initial_size);
static bool remove (const char *file);
static int open (const char *file);
static int filesize (int fd);
static int read (int fd, void *buffer, unsigned length);
static int write (int fd, const void *buffer, unsigned length);
static void seek (int fd, unsigned position);
static unsigned tell (int fd);
static void close (int fd);

#define CHECK_PTR(esp) \
{\
  if (!is_valid_address_of_thread (thread_current (), esp))\
    exit (-1);\
}

#define CHECK_PTR_RANGE(start, end) \
{\
  if (!is_valid_address_range_of_thread (thread_current (), start, end))\
    exit(-1);\
}

#define GET_PARAM(esp,type)\
({\
    CHECK_PTR(esp);\
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
  CHECK_PTR(f->esp);
  
  void *esp = f->esp;
  int syscall_id = *(int *)esp;
  esp += sizeof(int);

  int fd, status;
  pid_t pid;
  void *buffer;
  const void *buffer_cnst;
  const char *file;
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

      f->eax = read (fd, buffer, size); 
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
    
    break;
    case SYS_MUNMAP:
    
    break;
    case SYS_CHDIR:
    
    break;
    case SYS_MKDIR:
    
    break;
    case SYS_READDIR:
    
    break;
    case SYS_ISDIR:
    
    break;
    case SYS_INUMBER:
    
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
  CHECK_PTR(file);

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
  CHECK_PTR(file);
  return create_file(file, initial_size);
}

static bool remove (const char *file)
{
  CHECK_PTR(file);
  return remove_file(file);
}

static int open (const char *file)
{
  CHECK_PTR(file);
  return open_file(file);
}

static int filesize (int fd)
{
  return filelength_open_file (fd);
}

static int read (int fd, void *buffer, unsigned length)
{
  CHECK_PTR_RANGE(buffer, buffer + length);
  return read_open_file(fd, buffer, length);
}

/* Writes to the given fd. 
   NOTE: remember, "\n" is length 1 but also "a\n" is length 1
   */
static int write (int fd, const void *buffer, unsigned length)
{
  void *buf = (void *)buffer;
  CHECK_PTR_RANGE(buf, buf + length);
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
  close_open_file (fd);
}