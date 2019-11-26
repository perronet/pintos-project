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


void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  fsaccess_init();
}

static void
syscall_handler (struct intr_frame *f) 
{
  if (!is_valid_address_of_thread (thread_current (), f->esp))
    exit (-1);
  
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
      if (!is_valid_address_of_thread (thread_current (), esp))
        exit (-1);
      status = *(int *)esp;
      esp += sizeof(int);

      exit (status);
    break;
    case SYS_EXEC:
      if (!is_valid_address_of_thread (thread_current (), esp))
        exit (-1);
      file = *(char **)esp;
      esp += sizeof(char *);

      f->eax = exec (file);
    break;
    case SYS_WAIT:
      if (!is_valid_address_of_thread (thread_current (), esp))
        exit (-1);
      pid = *(pid_t *)esp;
      esp += sizeof(pid_t);
    
      f->eax = wait (pid);
    break;
    case SYS_CREATE:
      if (!is_valid_address_of_thread (thread_current (), esp))
        exit (-1);
      file = *(char **)esp;
      esp += sizeof(char *);
      if (!is_valid_address_of_thread (thread_current (), esp))
        exit (-1);
      initial_size = *(unsigned *)esp;
      esp += sizeof(unsigned); 
    
      f->eax = create (file, initial_size);
    break;
    case SYS_REMOVE:
      if (!is_valid_address_of_thread (thread_current (), esp))
        exit (-1);
      file = *(char **)esp;
      esp += sizeof(char *);

      f->eax = remove (file);
    break;
    case SYS_OPEN:
      if (!is_valid_address_of_thread (thread_current (), esp))
        exit (-1);
      file = *(char **)esp;
      esp += sizeof(char *);
    
      f->eax = open (file);
    break;
    case SYS_FILESIZE:
      if (!is_valid_address_of_thread (thread_current (), esp))
        exit (-1);
      fd = *(int *)esp;
      esp += sizeof(int); 
    
      f->eax = filesize (fd);
    break;
    case SYS_READ:
      if (!is_valid_address_of_thread (thread_current (), esp))
        exit (-1);
      fd = *(int *)esp;
      esp += sizeof(int);
      if (!is_valid_address_of_thread (thread_current (), esp))
        exit (-1);
      buffer = *(void **)esp;
      esp += sizeof(void *);
      if (!is_valid_address_of_thread (thread_current (), esp))
        exit (-1);
      size = *(unsigned *)esp;
      esp += sizeof(unsigned);   

      f->eax = read (fd, buffer, size); 
    break;
    case SYS_WRITE:
      if (!is_valid_address_of_thread (thread_current (), esp))
        exit (-1);
      fd = *(int *)esp;
      esp += sizeof(int);
      if (!is_valid_address_of_thread (thread_current (), esp))
        exit (-1);
      buffer_cnst = *(void **)esp;
      esp += sizeof(void *);
      if (!is_valid_address_of_thread (thread_current (), esp))
        exit (-1);
      size = *(unsigned *)esp;
      esp += sizeof(unsigned);

      f->eax = write (fd, buffer_cnst, size);
    break;
    case SYS_SEEK:
      if (!is_valid_address_of_thread (thread_current (), esp))
        exit (-1);
      fd = *(int *)esp;
      esp += sizeof(int);
      if (!is_valid_address_of_thread (thread_current (), esp))
        exit (-1); 
      position = *(unsigned *)esp;
      esp += sizeof(unsigned); 
    
      seek (fd, position);
    break;
    case SYS_TELL:
      if (!is_valid_address_of_thread (thread_current (), esp))
        exit (-1);
      fd = *(int *)esp;
      esp += sizeof(int); 
    
      f->eax = tell (fd);
    break;
    case SYS_CLOSE:
      if (!is_valid_address_of_thread (thread_current (), esp))
        exit (-1);
      fd = *(int *)esp;
      esp += sizeof(int); 
    
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
  //printf("HALT\n");
  shutdown_power_off ();
}

static void exit (int status)
{
  //printf("EXIT %d\n", status);
  thread_current ()->exit_status = status;
  close_all_files_of (thread_current ()->tid);
  thread_exit ();
}

static pid_t exec (const char *file)
{
  // printf("EXEC %p executing: %s\n", file, file);
  if (!is_valid_address_of_thread (thread_current (), file))
    exit (-1);

  struct thread *cur = thread_current ();
  cur->child_born_status = 0;
  tid_t tid = process_execute (file);

  sema_down (&cur->child_sema);
  if (cur->child_born_status == -1)
    return -1;
  else
    return tid;
}

static int wait (pid_t pid)
{
  //printf("WAIT %d\n", pid);
  return process_wait (pid);
}

static bool create (const char *file, unsigned initial_size)
{
  //printf("CREATE %p %d\n", file, initial_size);

  if (!is_valid_address_of_thread (thread_current (), file))
    exit (-1);

  return create_file(file, initial_size);
}

static bool remove (const char *file)
{
  //printf("REMOVE %p\n", file);

  if (!is_valid_address_of_thread (thread_current (), file))
    exit (-1);

  return remove_file(file);
}

static int open (const char *file)
{
  //printf("OPEN %p\n", file);

  if (!is_valid_address_of_thread (thread_current (), file))
    exit (-1);

  return open_file(file);
}

static int filesize (int fd)
{
  //printf("FILESIZE %d\n", fd);
  return filelength_open_file (fd);
}

static int read (int fd, void *buffer, unsigned length)
{
  //printf("READ %d %p %d\n", fd, buffer, length);
  
  struct thread * current = thread_current ();
  if (!is_valid_address_range_of_thread (current, buffer, buffer + length))
      exit(-1);

  return read_open_file(fd, buffer, length);
}

/* Writes to the given fd. 
   NOTE: remember, "\n" is length 1 but also "a\n" is length 1
   */
static int write (int fd, const void *buffer, unsigned length)
{
  //printf("WRITE %d %p %d need to write: %s\n", fd, buffer, length, (char *)buffer);
  
  struct thread * current = thread_current ();
  void *buf = (void *)buffer;
  if (!is_valid_address_range_of_thread (current, buf, buf + length))
    exit(-1);
  return write_open_file (fd, buf, length);
}

static void seek (int fd, unsigned position)
{
  //printf("SEEK %d %d\n", fd, position);
  seek_open_file (fd, position);
}

static unsigned tell (int fd)
{
  //printf("TELL %d\n", fd);
  return tell_open_file (fd);
}

static void close (int fd)
{
  //printf("CLOSE %d\n", fd);
  close_open_file (fd);
}