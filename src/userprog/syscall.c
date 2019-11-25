#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "syscall.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
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
}

static void
syscall_handler (struct intr_frame *f) 
{
  printf("TESTING: Stack status in syscall_handler\n");
  printf("my stack pointer is now in %p\n", f->esp);
  // hex_dump (((int)f->esp), f->esp, 1024, true);

  int syscall_id = *(int *)f->esp;
  f->esp += sizeof(int);
  printf ("system call number %d!\n", syscall_id);
  hex_dump (((int)f->esp), f->esp, 32, true);


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
      status = *(int *)f->esp;
      f->esp += sizeof(int);

      exit (status);
    break;
    case SYS_EXEC:
      file = *(char **)f->esp;
      f->esp += sizeof(char *);

      exec (file);
    break;
    case SYS_WAIT:
      pid = *(pid_t *)f->esp;
      f->esp += sizeof(pid_t);
    
      wait (pid);
    break;
    case SYS_CREATE:
      file = *(char **)f->esp;
      f->esp += sizeof(char *);
      initial_size = *(unsigned *)f->esp;
      f->esp += sizeof(unsigned); 
    
      create (file, initial_size);
    break;
    case SYS_REMOVE:
      file = *(char **)f->esp;
      f->esp += sizeof(char *);

      remove (file);
    break;
    case SYS_OPEN:
      file = *(char **)f->esp;
      f->esp += sizeof(char *);
    
      open (file);
    break;
    case SYS_FILESIZE:
      fd = *(int *)f->esp;
      f->esp += sizeof(int); 
    
      filesize (fd);
    break;
    case SYS_READ:
      fd = *(int *)f->esp;
      f->esp += sizeof(int);
      buffer = *(void **)f->esp;
      f->esp += sizeof(void *);
      size = *(unsigned *)f->esp;
      f->esp += sizeof(unsigned);   

      read (fd, buffer, size); 
    break;
    case SYS_WRITE:
      fd = *(int *)f->esp;
      f->esp += sizeof(int);
      buffer_cnst = *(void **)f->esp;
      f->esp += sizeof(void *);
      size = *(unsigned *)f->esp;
      f->esp += sizeof(unsigned);

      write (fd, buffer_cnst, size);
    break;
    case SYS_SEEK:
      fd = *(int *)f->esp;
      f->esp += sizeof(int); 
      position = *(unsigned *)f->esp;
      f->esp += sizeof(unsigned); 
    
      seek (fd, position);
    break;
    case SYS_TELL:
      fd = *(int *)f->esp;
      f->esp += sizeof(int); 
    
      tell (fd);
    break;
    case SYS_CLOSE:
      fd = *(int *)f->esp;
      f->esp += sizeof(int); 
    
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
  printf("HALT\n");
  shutdown_power_off ();
}

static void exit (int status)
{
  printf("EXIT %d\n", status);
  thread_current ()->exit_status = status;
  thread_exit ();
}

static pid_t exec (const char *file)
{
  printf("EXEC %p executing: %s\n", file, file);

  struct thread * current = thread_current ();

  if (!is_valid_address_of_thread (current, file))
    exit (-1);

  return 0;
}

static int wait (pid_t pid)
{
  printf("WAIT %d\n", pid);
  return 0;
}

static bool create (const char *file, unsigned initial_size)
{
  printf("CREATE %p %d\n", file, initial_size);
  return 0;
}

static bool remove (const char *file)
{
  printf("REMOVE %p\n", file);
  return 0;
}

static int open (const char *file)
{
  printf("OPEN %p\n", file);
  return 0;
}

static int filesize (int fd)
{
  printf("FILESIZE %d\n", fd);
  return 0;
}

static int read (int fd, void *buffer, unsigned length)
{
  printf("READ %d %p %d\n", fd, buffer, length);
  return 0;
}

//watch out: "\n" is length 1 but also "a\n" is length 1!!! (this is unfixable, it's in the printf code, deal with it)
static int write (int fd, const void *buffer, unsigned length)
{
  printf("WRITE %d %p %d need to write: %s\n", fd, buffer, length, (char *)buffer);
  
  struct thread * current = thread_current ();

  if (!is_valid_address_range_of_thread (current, buffer, buffer + length))
    exit (-1);

  // int result = 0;
  // if (fd == STDIN_FILENO)
  //     status = -1;
  // else if (fd == STDOUT_FILENO)
  //     putbuf (buffer, size);

  return 0;
}

static void seek (int fd, unsigned position)
{
  printf("SEEK %d %d\n", fd, position);
}

static unsigned tell (int fd)
{
  printf("TELL %d\n", fd);
  return 0;
}

static void close (int fd)
{
  printf("CLOSE %d\n", fd);
}

