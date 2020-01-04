#include "devices/block.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "filesys/fsaccess.h"
#include "swap.h"
#include <bitmap.h>
#include <stdbool.h>
#include <stddef.h>
#include <inttypes.h>


struct block  *swap_device;

static struct bitmap *swap_bm;
struct lock swap_lock;

void 
swap_init ()
{
  lock_init(&swap_lock);
  swap_device = block_get_role (BLOCK_SWAP);
  ASSERT (swap_device != NULL);
  size_t bmsize = block_size(swap_device) / SECTORS_PER_PAGE;
  swap_bm = bitmap_create(bmsize);
  ASSERT (swap_bm != NULL);

  bitmap_set_all(swap_bm, true);
} 

void 
swap_in (size_t slot, void* page)
{
  lock_acquire (&swap_lock);
  size_t swap_addr_base = slot * SECTORS_PER_PAGE;
  for (size_t i = 0; i < SECTORS_PER_PAGE; i++)
    {
      block_sector_t from = swap_addr_base + i;
      void* to = page + i * BLOCK_SECTOR_SIZE;
      lock_fs ();
      block_read (swap_device, from, to);
      unlock_fs ();
    }

  swap_free (slot);
  lock_release (&swap_lock);
}

size_t 
swap_out (const void* page)
{
  lock_acquire (&swap_lock);
  size_t slot = bitmap_scan_and_flip (swap_bm, 0, 1, true);

  if (slot != BITMAP_ERROR)
    {
      size_t swap_addr_base = slot * SECTORS_PER_PAGE;
      lock_fs ();
      for (size_t i = 0; i < SECTORS_PER_PAGE; i++)
        {
          const void* from = page + i * BLOCK_SECTOR_SIZE;
          block_sector_t to = swap_addr_base + i;
          block_write (swap_device, to, from);
        }
      unlock_fs ();
      lock_release (&swap_lock);
      return slot;  
    }
  else
    {
      lock_release (&swap_lock);
      return SWAP_ERROR;
    }
}

/* Call with lock acquired */
void swap_free(size_t slot)
{
  bitmap_flip (swap_bm, slot);
}
  