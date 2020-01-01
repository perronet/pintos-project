#include "userprog/pagedir.h"
#include "threads/malloc.h"
#include "filesys/file.h"
#include "frame.h"
#include "swap.h"
#include "page.h"
#include "threads/vaddr.h"
#include "threads/thread.h"
#include "filesys/fsaccess.h"

int last_map_id = 0;

static struct pt_suppl_entry *
pt_suppl_setup_file_info (struct file *file, off_t offset, uint8_t *page_addr, 
uint32_t read_bytes, uint32_t zero_bytes, bool writable, enum pt_status status);

void pt_suppl_init (struct hash *table)
{
  hash_init (table, pt_suppl_hash, pt_suppl_less, NULL);
}

struct pt_suppl_entry * pt_suppl_get_entry_by_addr(const void *vaddr)
{ 
  struct thread *current = thread_current();
  void * page = pg_round_down (vaddr);
  return pt_suppl_get (&current->pt_suppl, page);
}

bool pt_suppl_handle_page_fault (void * vaddr, struct intr_frame *f)
{
  ASSERT (vaddr != NULL && vaddr < PHYS_BASE);

  struct pt_suppl_entry *e = pt_suppl_get_entry_by_addr (vaddr);
  if (e != NULL)
    {
      return pt_suppl_page_in (e);
    }
  else
    {
      void * page = pg_round_down (vaddr);
      bool is_stack_growth = vaddr >= f->esp-32;
      is_stack_growth &= PHYS_BASE - page <= MAX_STACK;

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
  struct thread *curr = thread_current ();
  off_t length = file_length (f);
  int remaining;
  if (length == 0)
    return -1;

  last_map_id ++;

  void * page_addr = start_page; 
  for(int offset = 0; offset < length; offset += PGSIZE)
  {
    /* No page should be present */
    if (pt_suppl_get (&curr->pt_suppl, page_addr + offset) || 
      pagedir_get_page (curr->pagedir, page_addr + offset))
    {
      // printf("ERROR 2\n"); //TODO remove me
      return -1;
    }

    remaining = length - offset;
    if (remaining >= PGSIZE)
      remaining = PGSIZE;
    pt_suppl_add_mmf(f, offset, page_addr, remaining);

    page_addr += PGSIZE;
  }

  return last_map_id;
}

/* Unmaps all files of the current thread */
void 
unmap_all()
{
  struct thread *current = thread_current();
  struct hash_elem *del_elem;
  struct pt_suppl_entry *deleted;
  struct pt_suppl_entry entry;
  struct pt_suppl_file_info mmf;
  entry.vaddr = NULL; //trigger special search with map_ids
  mmf.map_id = -1; //trigger special search with owner;
  mmf.owner = current;
  entry.file_info = &mmf;
  entry.status = MMF;
      
  bool done = false;
  while (!done)
    {
      del_elem = hash_find (&current->pt_suppl, &entry.elem);
      if(del_elem)
      {
        deleted = hash_entry (del_elem, struct pt_suppl_entry, elem);
        ASSERT (deleted->file_info != NULL); //this search should only yield mmf
        pt_suppl_handle_unmap (deleted->file_info->map_id);
      }
      else
        done = true;
    }
}


void 
pt_suppl_handle_unmap (int map_id)
{
  bool removed = false;
  struct pt_suppl_entry entry;
  struct pt_suppl_file_info mmf;
  struct hash_elem *del_elem;
  struct pt_suppl_entry *deleted;
  struct thread *curr = thread_current();

  while (!removed)
    {
      mmf.map_id = map_id;
      entry.vaddr = NULL; //trigger special search with map_ids
      entry.file_info = &mmf;
      entry.status = MMF;
      // TODO
      // I'm assuming that this finds a page with that map id at every iteration, re-check this
      del_elem = hash_delete (&curr->pt_suppl, &entry.elem);

      if (del_elem != NULL)
      {
        deleted = hash_entry (del_elem, struct pt_suppl_entry, elem);

        if (pagedir_is_dirty (curr->pagedir, deleted->vaddr))
          pt_suppl_flush_mmf(deleted);

        if(deleted->file_info)
          close_open_file_direct (deleted->file_info->file);
        pt_suppl_destroy(deleted);
      } 
      else
      {
        removed = true;
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
                                  read_bytes, PGSIZE - read_bytes, true, MMF_UNLOADED);

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
  info->owner = thread_current();
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
      bool is_writable = entry->file_info == NULL || entry->file_info->writable;
      bool pagedir = pagedir_set_page (thread_current ()->pagedir,
                    entry->vaddr, page, is_writable);
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
      if(info->read_bytes > 0)
      {
        read = file_read (info->file, page, info->read_bytes);
        memset (page + info->read_bytes, 0, info->zero_bytes);
      }
      else
      {
        read = true;
        memset (page, 0, info->zero_bytes);
      }
      if (read)
        pagedir = pagedir_set_page (thread_current ()->pagedir,
                    entry->vaddr, page, info->writable);

      if(pagedir)
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

/* The less function supports three different kinds of search
   - searching for vaddr
   - searching for map_id
   - searching for owner.
   Note that the elements inside the table cannot have vaddr == NULL
   and hence cannot trigger any special search. 

   The only elements with this property are the elements passed to the 
   find function of the hash table.
   - All special searches require to set the status to MMF
   - By setting vaddr to NULL, trigger special search with map_ids
   - By setting map_id to -1, trigger special search with owner.
   When we want elements to not match, we simply return true.
   This is due to the internals of the hash table 
   (That assumes a total order: !a<b && !b<a -> a == b)
   */
bool 
pt_suppl_less (const struct hash_elem *ha, 
        const struct hash_elem *hb,
        void *aux UNUSED)
{
  struct pt_suppl_entry *a,*b;

  a = hash_entry (ha, struct pt_suppl_entry, elem);
  b = hash_entry (hb, struct pt_suppl_entry, elem);

  /*Special case to handle searches for map_id*/
  if(a->vaddr == NULL || b->vaddr == NULL)
  {
    ASSERT(a->file_info != NULL || b->file_info != NULL);
    
    if(GET_TYPE(a->status) != GET_TYPE(b->status))
      return true; //These elements are actually incomparable

    if(a->file_info->map_id < 0 || b->file_info->map_id < 0)
      return a->file_info->owner < b->file_info->owner; //special case, owner search
    else
      return a->file_info->map_id < b->file_info->map_id; //special case, map_id search
  }
  return a->vaddr < b->vaddr;
}
