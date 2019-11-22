#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

static void syscall_handler (struct intr_frame *);
// static void halt (void) NO_RETURN;
// static void exit (int status) NO_RETURN;
// static pid_t exec (const char *file);
// static int wait (pid_t);
// static bool create (const char *file, unsigned initial_size);
// static bool remove (const char *file);
// static int open (const char *file);
// static int filesize (int fd);
// static int read (int fd, void *buffer, unsigned length);
// static int write (int fd, const void *buffer, unsigned length);
// static void seek (int fd, unsigned position);
// static unsigned tell (int fd);
// static void close (int fd);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  // printf("my stack pointer is now in %p\n", f->esp);
  // hex_dump (((int)f->esp), f->esp, 1024, true);
  printf ("system call!\n");
  thread_exit ();
}

// static void halt ()
// {

// }

// static void exit (int status)
// {

// }

// static pid_t exec (const char *file)
// {
//   return 0;
// }

// static int wait (pid_t)
// {
//   return 0;
// }

// static bool create (const char *file, unsigned initial_size)
// {
//   return 0;
// }

// static bool remove (const char *file)
// {
//   return 0;
// }

// static int open (const char *file)
// {
//   return 0;
// }

// static int filesize (int fd)
// {
//   return 0;
// }

// static int read (int fd, void *buffer, unsigned length)
// {
//   return 0;
// }

// static int write (int fd, const void *buffer, unsigned length)
// {
//   return 0;
// }

// static void seek (int fd, unsigned position)
// {

// }

// static unsigned tell (int fd)
// {
//   return 0;
// }

// static void close (int fd)
// {

// }