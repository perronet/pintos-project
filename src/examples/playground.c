#include <stdio.h>
#include <syscall.h>

int
main (int argc, char **argv)
{
  // Just test syscalls here
  // for (i = 0; i < argc; i++)
  //   printf ("%s ", argv[i]);
  // printf ("test\n");

  //SYSCALLS WITHOUT RETURN
  // exit(5);
  //halt ();

  //SYSCALLS WITH RETURN
  // write (1, "test", 4);
  // close (fd);
  // exec("echo");
  create ("testfile", 40);

  // int fd = open ("newnewfile");
  // printf("The file size is %d\n", filesize (fd));

  return EXIT_SUCCESS;
}
