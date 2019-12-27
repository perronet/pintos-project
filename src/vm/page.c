#include "userprog/pagedir.h"
#include "threads/malloc.h"
#include "filesys/file.h"
#include "frame.h"
#include "swap.h"
#include "page.h"
#include "threads/vaddr.h"
#include "threads/thread.h"

int last_map_id = 0;

static struct pt_suppl_entry *
pt_suppl_setup_file_info (struct file *file, off_t offset, uint8_t *page_addr, 
uint32_t read_bytes, uint32_t zero_bytes, bool writable, enum pt_status status);


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
          return false;  
    } 
}

int 
pt_suppl_handle_mmap (struct file *f, void *start_page)
{
  off_t length = file_length (f);
  if (length == 0)
    return -1;

  last_map_id ++;

  void * page_addr = start_page; 
  for(int offset = 0; offset < length; offset += PGSIZE)
  {
    int remaining = length - offset;
    if (remaining > PGSIZE)
      remaining = PGSIZE;
    page_addr += PGSIZE;

    pt_suppl_add_mmf(f, offset, page_addr, remaining);
  }

  return last_map_id;
}

void 
pt_suppl_handle_unmap (int map_id)
{
  bool removed = false;
  struct thread *current = thread_current();

  while (!removed)
    {
      struct pt_suppl_entry entry;
      struct pt_suppl_file_info mmf;
      mmf.map_id = map_id;
      entry.file_info = &mmf;
      struct hash_elem *del_elem;
      del_elem = hash_delete (&current->pt_suppl, &entry.elem);

      struct pt_suppl_entry *deleted;
      deleted = hash_entry (del_elem, struct pt_suppl_entry, elem);
      if (pagedir_is_dirty (current->pagedir, deleted->vaddr))
        pt_suppl_flush_mmf(deleted);

      pt_suppl_destroy(deleted);
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
  hash_insert (table, &entry->elem);
  return true;
}

void 
pt_suppl_destroy(struct pt_suppl_entry *entry)
{
  ASSERT (entry != NULL);
  
  if(entry->file_info != NULL)
    free (entry->file_info);
  free (entry);
}

bool
pt_suppl_add_mmf (struct file *file, off_t offset, uint8_t *page_addr, 
uint32_t read_bytes)
{
  struct pt_suppl_entry * entry = pt_suppl_setup_file_info (file, offset, page_addr, 
                                  read_bytes, 0, false, MMF_UNLOADED);

  return entry != NULL;
}

void pt_suppl_flush_mmf (struct pt_suppl_entry *entry)
{
  if(entry->status == MMF_PRESENT || 
     entry->status == MMF_SWAPPED)
    {
      struct pt_suppl_file_info *mmf = entry->file_info;
      ASSERT (mmf != NULL);

      file_seek (mmf->file, mmf->offset);
      file_write (mmf->file, entry->vaddr, mmf->read_bytes);
    }
}

bool
pt_suppl_add_lazy (struct file *file, off_t offset, uint8_t *page_addr, 
                uint32_t read_bytes, uint32_t zero_bytes, bool writable)
{
  struct pt_suppl_entry * entry = pt_suppl_setup_file_info (file, offset, page_addr, 
                                  read_bytes, zero_bytes, writable, LAZY_UNLOADED);

  return entry != NULL;
}

static struct pt_suppl_entry *
pt_suppl_setup_file_info (struct file *file, off_t offset, uint8_t *page_addr, 
      uint32_t read_bytes, uint32_t zero_bytes, bool writable, enum pt_status status)
{
  struct pt_suppl_entry * entry = calloc (1, sizeof (struct pt_suppl_entry));

  if(entry == NULL)
    return NULL;

  struct pt_suppl_file_info * info = calloc (1, sizeof (struct pt_suppl_file_info));
  if(info == NULL)
  {
    free(entry);
    return NULL;
  } 

  entry->vaddr = page_addr;
  entry->status = status;
  entry->file_info = info;
  info->file = file;
  info->offset = offset;
  info->map_id = last_map_id;
  info->read_bytes = read_bytes;
  info->zero_bytes = zero_bytes;
  info->writable = writable;

  bool success = pt_suppl_add (&thread_current ()->pt_suppl, entry);
  ASSERT (success);
  return entry;
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
      struct pt_suppl_file_info *info = entry->file_info;
      ASSERT (info != NULL);

      bool read = false, pagedir = false;
      file_seek (info->file, info->offset);
      read = file_read (info->file, page, info->read_bytes);
      //TODO memset (page + info.read_bytes, 0, zero_bytes);
      if (read)
        pagedir = pagedir_set_page (thread_current ()->pagedir,
                    entry->vaddr, page, info->writable);

      bool success = read && pagedir;
      if(success)
        {
          SET_PRESENCE(entry->status, PRESENT);
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
      free (entry->file_info);
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

  if(IS_MMF(a->status) && IS_MMF(b->status)) //hammered
    return a->file_info->map_id < b->file_info->map_id;
  else  
    return a->vaddr < b->vaddr;
}
