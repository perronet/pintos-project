#include <stdio.h>
#include <syscall.h>

int
main (int argc, char **argv)
{
  int i;

  // Just test syscalls here
  // for (i = 0; i < argc; i++)
  //   printf ("%s ", argv[i]);
  // printf ("\n");

  // exit (EXIT_SUCCESS);
  halt ();

  return EXIT_SUCCESS;
}
