#ifndef _FRAME_H
#define _FRAME_H

#include "threads/palloc.h"
#include <bitmap.h>
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "threads/loader.h"
#include "threads/synch.h"
#include "threads/vaddr.h"

struct frame_entry
{
  void *page;
  tid_t tid;
  //other info to implement LRU page eviciton policy
  struct list_elem elem;
};

void vm_frame_alloc_init (void);
void *vm_frame_alloc_multiple (enum palloc_flags flags, size_t page_cnt);
void *vm_frame_alloc (enum palloc_flags flags);
void vm_frame_free (void *page);
bool frame_list_add (void *page, enum palloc_flags flags);
void frame_list_remove (struct list_elem *e);
void frame_evict (struct frame_entry frame); 

#endif
