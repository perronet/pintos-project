#include "frame.h" 

static struct list frame_list;
static struct lock frame_list_lock;

void vm_frame_alloc_init ()
{
  list_init (&frame_list);
  lock_init (&frame_list_lock);
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
          if (!frame_list_add (pages, flags))
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
  struct list_elem *e;
  struct frame_entry *frame = NULL;

  /* Remove from list */
  lock_acquire (&frame_list_lock);
  for (e = list_begin (&frame_list); e != list_end (&frame_list);
     e = list_next (e))
    {
      frame = list_entry (e, struct frame_entry, elem);
      if (frame->page == page)
        {
          list_remove (e);
          free (frame);
          break;
        }
    }
  lock_release (&frame_list_lock);

  /* Free the page */
  palloc_free_page (page);
}

bool frame_list_add (void *page, enum palloc_flags flags)
{
  struct frame_entry *frame = malloc (sizeof(struct frame_entry));

  if (frame == NULL)
    return false;

  frame->page = page;
  lock_acquire (&frame_list_lock);

  if (flags & PAL_USER)
    list_push_back (&frame_list, &frame->elem);

  lock_release (&frame_list_lock);

  return true;
}

// void frame_list_remove (struct list_elem *e)
// {
//   lock_acquire (&frame_list_lock);
//   list_remove (e);
//   lock_release (&frame_list_lock);
//   free ( list_entry(e, struct frame_entry, elem));
// }

void frame_evict (struct frame_entry frame UNUSED)
{

}