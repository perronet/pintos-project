#include "frame.h" 
#include "page.h" 

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
    struct hash_elem e = frame->elem;
    hash_insert (&frame_hash, &e);
  }

  lock_release (&frame_hash_lock);

  return true;
}

struct frame_entry * evict_and_get_frame()
{
  //TODO LOCK

  struct frame_entry *victim = select_frame_to_evict();
  ASSERT (victim != NULL);

  pt_suppl_page_out (&victim->owner->pt_suppl, victim->page);

  //TODO SAVE FRAME


  return NULL;
}

struct frame_entry * select_frame_to_evict(void)
{
  return NULL; //TODO
}