#include "userprog/pagedir.h"
#include "threads/malloc.h"
#include "filesys/file.h"
#include "frame.h"
#include "swap.h"
#include "page.h"
#include "threads/vaddr.h"
#include "threads/thread.h"

void pt_suppl_init (struct hash *table)
{
  hash_init (table, pt_suppl_hash, pt_suppl_less, NULL);
}

bool pt_suppl_handle_page_fault (void * vaddr, struct intr_frame *f)
{
  ASSERT (vaddr != NULL && vaddr < PHYS_BASE);

  struct thread *current = thread_current();
  void * page = pg_round_down (vaddr);
  struct pt_suppl_entry * e = pt_suppl_get (&current->pt_suppl, page);

  if (e != NULL)
    {
      pt_suppl_page_in (e);
      return true;
    }
  else
    {
      bool is_stack_growth = vaddr < f->esp; //TODO check of oneoff errors
      //TODO check for downward limit (stack overflow)
      if (is_stack_growth)
        {
          pt_suppl_grow_stack(vaddr);
          return true;
        }
      else
        {
          return false;  
        }
    } 
}

struct pt_suppl_entry * 
pt_suppl_get (struct hash *table, void *page)
{
  struct pt_suppl_entry entry;
  entry.vaddr = page;
  struct hash_elem *el = hash_find (table, &entry.elem);

  if(el == NULL)
    return NULL;
  else
    return hash_entry (el, struct pt_suppl_entry, elem);
}

bool 
pt_suppl_add (struct hash *table, struct pt_suppl_entry *entry)
{
  ASSERT (table != NULL && entry != NULL);
  return hash_insert (table, &entry->elem) != NULL;
}

void 
pt_suppl_remove (struct hash *table, struct pt_suppl_entry *entry)
{
  ASSERT (table != NULL && entry != NULL);
  hash_delete (table, &entry->elem);
  
  if(entry->mmf != NULL)
    free (entry->mmf);
  free (entry);
}

bool
pt_suppl_add_mmf (struct file *file, off_t offset, uint8_t *page_addr, 
uint32_t read_bytes)
{
  struct pt_suppl_entry * entry = calloc (1, sizeof (struct pt_suppl_entry));

  if(entry == NULL)
    return false;

  struct pt_suppl_mmf * mmf = calloc (1, sizeof (struct pt_suppl_mmf));
  if(mmf == NULL)
  {
    free(entry);
    return false;
  }

  entry->vaddr = page_addr;
  entry->status = MMF_UNLOADED;
  entry->mmf = mmf;
  mmf->file = file;
  mmf->offset = offset;
  mmf->read_bytes = read_bytes;

  bool success = pt_suppl_add (&thread_current ()->pt_suppl, entry);
  ASSERT (success);

  return success;
}

void pt_suppl_flush_mmf (struct pt_suppl_entry *entry)
{
  if(entry->status == MMF_PRESENT || 
     entry->status == MMF_SWAPPED)
    {
      struct pt_suppl_mmf *mmf = entry->mmf;
      ASSERT (mmf != NULL);

      file_seek (mmf->file, mmf->offset);
      file_write (mmf->file, entry->vaddr, mmf->read_bytes);
    }
}

bool pt_suppl_page_in (struct pt_suppl_entry *entry)
{
  uint8_t *page = vm_frame_alloc (PAL_USER);
  if (page == NULL) return false;

  if (IS_SWAPPED (entry->status))
    {
      bool pagedir = pagedir_set_page (thread_current ()->pagedir,
                    entry->vaddr, page, true /*TODO is writable?*/);
      if (!pagedir)
      {
        vm_frame_free (page);
        return false;
      }

      swap_in (entry->swap_slot, entry->vaddr);

      //TODO DELETE FROM pt_suppl IF NOT MMF? 
      entry->status = entry->status == SWAPPED ? PRESENT : MMF_PRESENT;
    }
  else if (IS_UNLOADED (entry->status))
    {
      struct pt_suppl_mmf *mmf = entry->mmf;
      ASSERT (mmf != NULL);

      bool read = false, pagedir = false;
      file_seek (mmf->file, mmf->offset);
      read = file_read (mmf->file, page, mmf->read_bytes);
      //TODO memset (page + mmf.read_bytes, 0, zero_bytes);
      if (read)
        pagedir = pagedir_set_page (thread_current ()->pagedir,
                    entry->vaddr, page, true /*TODO is writable?*/);

      bool success = read && pagedir;
      if(success)
        {
          entry->status = MMF_PRESENT;
          return true;
        }
      else
        {
          vm_frame_free (page);
          return false;
        }
    }
  else PANIC ("Trying to page-in already loaded page");
  return false;
}

void 
pt_suppl_page_out (struct hash *table UNUSED, void *page UNUSED)
{
  //TODO
}


void
pt_suppl_grow_stack (void *top)
{
  void *page = vm_frame_alloc(PAL_USER | PAL_ZERO);
  if (page != NULL)
  {
    bool success = pagedir_set_page 
          (thread_current ()->pagedir, pg_round_down (top), page, true);

    if(!success)
      vm_frame_free (page);
  }
}

static void
pt_suppl_free_entry (struct hash_elem *he, void *aux UNUSED)
{
  struct pt_suppl_entry *entry;
  entry = hash_entry (he, struct pt_suppl_entry, elem);
  if (entry->status == MMF_SWAPPED)
    {
      /*TODO clear swap slot*/
      free (entry->mmf);
    }
  free (entry);
}

void 
pt_suppl_free (struct hash *table) 
{
  hash_destroy (table, pt_suppl_free_entry);
}

//**** Hash table functionalities

unsigned 
pt_suppl_hash (const struct hash_elem *he, void *aux UNUSED)
{
  struct pt_suppl_entry *pe = 
    hash_entry (he, struct pt_suppl_entry, elem);
  return hash_bytes (&pe->vaddr, sizeof pe->vaddr);
}

bool 
pt_suppl_less (const struct hash_elem *ha, 
        const struct hash_elem *hb,
        void *aux UNUSED)
{
  struct pt_suppl_entry *a,*b;
  a = hash_entry (ha, struct pt_suppl_entry, elem);
  b = hash_entry (hb, struct pt_suppl_entry, elem);
  
  return a->vaddr < b->vaddr;
}
