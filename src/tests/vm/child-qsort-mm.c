/* Mmaps a 128 kB file "sorts" the bytes in it, using quick sort,
   a multi-pass divide and conquer algorithm.  */

#include <debug.h>
#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"
#include "tests/vm/qsort.h"
#include <stdio.h>

const char *test_name = "child-qsort-mm";

#define CHUNK_SIZE (128 * 1024)

int
main (int argc UNUSED, char *argv[]) 
{
  int handle;
  unsigned char *p = (unsigned char *) 0x10000000;

  quiet = true;

  CHECK ((handle = open (argv[1])) > 1, "open \"%s\"", argv[1]);
  CHECK (mmap (handle, p) != MAP_FAILED, "mmap \"%s\"", argv[1]);

  int child_num = (int)argv[1][3]-47;
  printf("I am child %d\n", child_num);
  for (unsigned int j = 0; j < CHUNK_SIZE-1; j++)
  {
  	if (*(p+j) == 254)
  	{
  		continue;
  	}
  	ASSERT (*(p+j) < *(p+j+1));
  }

  qsort_bytes (p, 1024 * 128);
  
  return 80;
}
