#include <stdio.h>
#include <syscall.h>

int
main (int argc, char **argv)
{
  // Just test syscalls here
  // for (i = 0; i < argc; i++)
  //   printf ("%s ", argv[i]);
  // printf ("\n");

  exec(0);
  // exit (EXIT_SUCCESS);
  //halt ();

  return EXIT_SUCCESS;
}
