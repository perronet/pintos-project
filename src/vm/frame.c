#include "frame.h" 
#include "page.h" 
#include "threads/malloc.h"
#include "userprog/pagedir.h"

static struct hash frame_hash;
static struct lock frame_hash_lock;

unsigned find_frame (const struct hash_elem *e, void *aux UNUSED)
{
  struct frame_entry *frame = hash_entry (e, struct frame_entry, elem);
  return hash_bytes (&frame->page, sizeof (frame->page));
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

void *vm_frame_alloc (enum palloc_flags flags)
{
  void *page = NULL;

  /* Must request allocation from the user pool, kernel memory is non-pageable */
  if (flags & PAL_USER)
    page = palloc_get_page (flags);

  if (page != NULL)
    {
      bool added = frame_hash_add (page, flags);
      if (!added)
        PANIC ("Out of memory!");
    }
  else
    {
      struct frame_entry * f = evict_and_get_frame();
      ASSERT (f != NULL);
      page = f->page;
    }

  return page;

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
    hash_insert (&frame_hash, &frame->elem);
  }

  lock_release (&frame_hash_lock);

  return true;
}

struct frame_entry * evict_and_get_frame()
{
  struct thread *t = thread_current ();

  lock_acquire (&frame_hash_lock);

  struct frame_entry *victim = select_frame_to_evict();
  if (victim == NULL)
    PANIC ("No frame to evict");

  /* Save old frame before modifying the frame entry */
  if (!save_evicted_frame (victim))
    PANIC ("Can't save evicted frame");

  pt_suppl_page_out (&victim->owner->pt_suppl, victim->page);
  victim->owner = t;

  lock_release (&frame_hash_lock);

  return victim;
}

struct frame_entry * select_frame_to_evict()
{
  struct hash_iterator i;
  struct frame_entry *f = NULL;
  bool found = false;

  while (!found)
  {
    hash_first (&i, &frame_hash);
    while (hash_next (&i))
    {
      f = hash_entry (hash_cur (&i), struct frame_entry, elem);
      if (pagedir_is_accessed (f->owner->pagedir, f->page))
      {
        /* Give second chance */
        pagedir_set_accessed (f->owner->pagedir, f->page, false);
      }
      else
      {
        found = true;
      }
    }
  }
  return f;
}

bool save_evicted_frame (struct frame_entry *f UNUSED)
{

  // struct pt_suppl_entry * pt_entry = pt_suppl_get ();

  // if (MMF_PRESENT && pagedir_is_dirty (f->owner->pagedir, f->page))
  // {

  // }
  // else if 
  // {

  // }
  // else
  // {

  // }

  return true;
}