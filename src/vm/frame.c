#include "frame.h" 

static struct hash frame_hash;
static struct lock frame_hash_lock;

unsigned find_frame (const struct hash_elem *e, void *aux UNUSED)
{
  struct frame_entry *frame = hash_entry (e, struct frame_entry, elem);
  return hash_bytes (frame->page, sizeof (frame->page));
}

bool compare_frame (const struct hash_elem *e1, const struct hash_elem *e2, void *aux UNUSED)
{
  struct frame_entry *frame1 = hash_entry (e1, struct frame_entry, elem);
  struct frame_entry *frame2 = hash_entry (e2, struct frame_entry, elem);
  return frame1->page < frame2->page;
}

void vm_frame_alloc_init ()
{
  hash_init (&frame_hash, find_frame, compare_frame, NULL);
  lock_init (&frame_hash_lock);
}

void *vm_frame_alloc_multiple (enum palloc_flags flags, size_t page_cnt)
{
  void *cpy, *pages = NULL;
  return palloc_get_multiple (flags, page_cnt);

  /* Must request allocation from the user pool, kernel memory is non-pageable */
  if (flags & PAL_USER)
    pages = palloc_get_multiple (flags, page_cnt);

  cpy = pages;
  if (pages != NULL)
    {
      for (int i = 0; i < (int)page_cnt; i++)
        {
          if (!frame_hash_add (pages, flags))
            PANIC ("Out of memory!");
          cpy += PGSIZE;
        }
    }
  else
    {
      // Perform swap, panic for now
      // PANIC ("No free frames left in memory!");
    }

  return pages;
}

void *vm_frame_alloc (enum palloc_flags flags)
{
  return vm_frame_alloc_multiple (flags, 1);
}

void vm_frame_free (void *page)
{
  struct hash_elem *e;
  struct frame_entry find;

  find.page = page;
  /* Remove from list */
  lock_acquire (&frame_hash_lock);
  e = hash_find (&frame_hash, &find.elem);
  hash_delete (&frame_hash, e);
  lock_release (&frame_hash_lock);

  free (hash_entry (e, struct frame_entry, elem));
  /* Free the page */
  palloc_free_page (page);
}

bool frame_hash_add (void *page, enum palloc_flags flags)
{
  struct frame_entry *frame = malloc (sizeof(struct frame_entry));

  if (frame == NULL)
    return false;

  frame->page = page;
  lock_acquire (&frame_hash_lock);

  if (flags & PAL_USER){
    struct hash_elem e = frame->elem;
    hash_insert (&frame_hash, &e);
  }

  lock_release (&frame_hash_lock);

  return true;
}

void frame_evict (struct frame_entry frame UNUSED)
{

}