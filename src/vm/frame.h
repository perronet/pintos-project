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
#include "lib/kernel/hash.h"
#include "threads/thread.h"

struct frame_entry
{
  void *page;
  struct thread *owner;
  void *thread_vaddr;

  struct hash_elem elem;
};

unsigned find_frame (const struct hash_elem *e, void *aux UNUSED);
bool compare_frame (const struct hash_elem *e1, const struct hash_elem *e2, void *aux UNUSED);
void frame_install_page (void *upage, void *kpage);
void vm_frame_alloc_init (void);
void *vm_frame_alloc (enum palloc_flags flags, void *thread_vaddr);
void vm_frame_free (void *page);
bool frame_hash_add (void *page, enum palloc_flags flags, void *thread_vaddr);
void frame_hash_remove (struct list_elem *e);
bool page_out_evicted_frame (struct frame_entry *f);
struct frame_entry * evict_and_get_frame(void); 
struct frame_entry * select_frame_to_evict(void); 

#endif
