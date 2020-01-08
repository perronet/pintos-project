#include "frame.h" 
#include "page.h" 
#include "swap.h"
#include "threads/malloc.h"
#include "userprog/pagedir.h"
#include "filesys/file.h"
#include "filesys/fsaccess.h"

static struct hash frame_hash;
static struct lock frame_hash_lock;

unsigned find_frame (const struct hash_elem *e, void *aux UNUSED)
{
  struct frame_entry *frame = hash_entry (e, struct frame_entry, elem);
  return hash_bytes (&frame->kpage, sizeof (frame->kpage));
}

bool compare_frame (const struct hash_elem *e1, const struct hash_elem *e2, void *aux UNUSED)
{
  struct frame_entry *frame1 = hash_entry (e1, struct frame_entry, elem);
  struct frame_entry *frame2 = hash_entry (e2, struct frame_entry, elem);
  return frame1->kpage < frame2->kpage;
}

void vm_frame_alloc_init ()
{
  hash_init (&frame_hash, find_frame, compare_frame, NULL);
  lock_init (&frame_hash_lock);
}

void *vm_frame_alloc (enum palloc_flags flags, void *thread_vaddr)
{
  void *page = NULL;
  /* Must request allocation from the user pool, kernel memory is non-pageable */
  if (flags & PAL_USER)
    page = palloc_get_page (flags);

  if (page != NULL)
    {
      bool added = frame_hash_add (page, flags, pg_round_down(thread_vaddr));
      if (!added)
        PANIC ("Out of memory!");
    }
  else
    {
      struct frame_entry *f = evict_and_get_frame();
      ASSERT (f != NULL);
      struct owner_info *own_info = malloc (sizeof (struct owner_info));
      own_info->owner = thread_current ();
      own_info->upage = thread_vaddr;
      list_push_back (&f->owners, &own_info->elem);
      page = f->kpage;
    }

  return page;
}

void vm_frame_free (void *page)
{
  struct hash_elem *e;
  struct frame_entry find;

  find.kpage = page;
  /* Remove from list */
  lock_acquire (&frame_hash_lock);
  e = hash_find (&frame_hash, &find.elem);
  hash_delete (&frame_hash, e);
  lock_release (&frame_hash_lock);

  struct frame_entry *remove = hash_entry (e, struct frame_entry, elem);
  free_owners (remove);
  free (remove);
  palloc_free_page (page);
}

bool frame_hash_add (void *page, enum palloc_flags flags, void *thread_vaddr)
{
  struct frame_entry *frame = malloc (sizeof(struct frame_entry));
  struct owner_info *own_info = malloc (sizeof(struct owner_info));

  if (frame == NULL)
    return false;

  frame->kpage = page;
  own_info->owner = thread_current ();
  own_info->upage = thread_vaddr;
  list_init (&frame->owners);
  list_push_back (&frame->owners, &own_info->elem);

  lock_acquire (&frame_hash_lock);

  if (flags & PAL_USER)
    hash_insert (&frame_hash, &frame->elem);

  lock_release (&frame_hash_lock);

  return true;
}

struct frame_entry * evict_and_get_frame()
{
  lock_acquire (&frame_hash_lock);

  struct frame_entry *victim = select_frame_to_evict();
  if (victim == NULL)
    PANIC ("No frame to evict");

  if (!page_out_evicted_frame (victim))
    PANIC ("Can't page out evicted frame");

  lock_release (&frame_hash_lock);

  return victim;
}

/* Call only with lock acquired */
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
      /* If no owner has accessed the page, then evict */
      if (!pagedir_check_accessed_foreach_owner (f))
      {
        found = true;
        break;
      }
    }
  }
  return f;
}

/* Only call with lock acquired */
bool page_out_evicted_frame (struct frame_entry *f)
{
  bool is_shared = (list_size (&f->owners) > 1);
  struct list_elem *e = list_begin (&f->owners);
  struct owner_info *own_info = list_entry (e, struct owner_info, elem);
  struct pt_suppl_entry *pt_entry = pt_suppl_get (&own_info->owner->pt_suppl, own_info->upage);
  size_t swap_slot_id;

  if (pt_entry == NULL)
    {// Lazy loaded -> swap out
      swap_slot_id = swap_out (own_info->upage);
      if ((int)swap_slot_id == SWAP_ERROR){
        PANIC ("Cannot swap");
        return false;
      }

      for (e = list_begin (&f->owners); e != list_end (&f->owners); e = list_next (e))
      {
        own_info = list_entry (e, struct owner_info, elem);
        pt_entry = pt_suppl_get (&own_info->owner->pt_suppl, own_info->upage);
        ASSERT (pt_entry == NULL);

        pt_entry = malloc (sizeof (struct pt_suppl_entry));
        pt_entry->vaddr = own_info->upage;
        pt_entry->swap_slot = swap_slot_id;
        pt_entry->file_info = NULL;
        SET_TYPE(pt_entry->status, LAZY);
        SET_PRESENCE (pt_entry->status, SWAPPED);
        hash_insert (&own_info->owner->pt_suppl, &pt_entry->elem);
        pagedir_clear_page (own_info->owner->pagedir, own_info->upage);
      }
      free_owners (f);
    }
  else if (IS_MMF (pt_entry->status))
    {// MMF -> write back to file if dirty
      ASSERT (!is_shared);
      if(pagedir_is_dirty (own_info->owner->pagedir, pt_entry->vaddr))
      {
        lock_fs ();
        file_write_at (pt_entry->file_info->file, pt_entry->vaddr, 
          pt_entry->file_info->read_bytes, pt_entry->file_info->offset);
        unlock_fs ();
      }
      SET_TYPE(pt_entry->status, MMF);
      SET_PRESENCE (pt_entry->status, UNLOADED);
      pagedir_clear_page (own_info->owner->pagedir, pt_entry->vaddr);
    }
  else
    {
      PANIC ("STATUS of page %p of %d is %d\n", pt_entry->vaddr, own_info->owner->tid, pt_entry->status);
    }

  return true;
}

bool pagedir_check_accessed_foreach_owner (struct frame_entry *f)
{
  struct list_elem *e;
  bool accessed = false;

  for (e = list_begin (&f->owners); e != list_end (&f->owners); e = list_next (e))
    {
      struct owner_info *own_info = list_entry (e, struct owner_info, elem);

      /* Update access bits */
      if (pagedir_is_accessed (own_info->owner->pagedir, own_info->upage)){
        accessed = true;
        pagedir_set_accessed (own_info->owner->pagedir, own_info->upage, false);
      }
    }

  return accessed;
}

void free_owners (struct frame_entry *f)
{
  struct list_elem *e = list_begin (&f->owners);

  while (e != list_end (&f->owners))
  {
    struct list_elem *remove = e;
    struct owner_info *own_info_remove = list_entry (remove, struct owner_info, elem);
    e = list_next (e);
    list_remove (remove);
    free (own_info_remove);
  }
}
