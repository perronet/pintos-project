#include "filesys/filesys.h"
#include "threads/malloc.h"
#include "lib/debug.h"
#include "lib/string.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "devices/timer.h"
#include "cache.h"

static void bc_move_to_bottom (struct buffer_cache_entry *entry);
static void bc_flush (struct buffer_cache_entry *entry);
static struct buffer_cache_entry * bc_get_entry_by_sector (block_sector_t sector);
static struct buffer_cache_entry * bc_get_free_entry (void);
static struct buffer_cache_entry * bc_evict (void);
static void bf_daemon(void *aux);

struct list cache;
struct lock cache_lock;
int cache_count;
bool daemon_started;

void bc_init ()
{
  list_init(&cache);
  lock_init(&cache_lock);
  daemon_started = false;

  cache_count = 0;
}

void bc_start_daemon ()
{
  ASSERT (!daemon_started);
  tid_t t = thread_create("buffer cache daemon", PRI_DEFAULT, bf_daemon, NULL);
  ASSERT (t != TID_ERROR);
  daemon_started = true;
}

void bc_block_read (block_sector_t sector, void *buffer, off_t offset, off_t size)
{
  ASSERT (offset + size <= BLOCK_SECTOR_SIZE);

  lock_acquire(&cache_lock);

  struct buffer_cache_entry *cache_entry = bc_get_entry_by_sector(sector);

  if (cache_entry != NULL)
    {/* CACHE HIT */
      bc_move_to_bottom (cache_entry);
    }
  else
    {/* CACHE FAULT */
      cache_entry = bc_get_free_entry ();
      cache_entry->sector = sector;
      cache_entry->is_dirty = false;
      block_read (fs_device, sector, cache_entry->data);
    }

  ASSERT (cache_entry != NULL);
  cache_entry->is_in_second_chance = false;
  memcpy (buffer, cache_entry->data + offset, size);

  lock_release(&cache_lock);
}

void bc_block_write (block_sector_t sector, void *buffer, off_t offset, off_t size)
{
  ASSERT (offset + size <= BLOCK_SECTOR_SIZE);

  lock_acquire(&cache_lock);

  struct buffer_cache_entry *cache_entry = bc_get_entry_by_sector(sector);

  if (cache_entry != NULL)
    {/* CACHE HIT */
      bc_move_to_bottom (cache_entry);
    }
  else
    {/* CACHE FAULT */
      cache_entry = bc_get_free_entry ();
      cache_entry->sector = sector;
      
      /* If the sector contains data before or after the chunk
         we're writing, then we need to read in the sector
         first.  Otherwise we start with a sector of all zeros. */
      if (offset > 0 || 
          size + offset < BLOCK_SECTOR_SIZE) 
        block_read (fs_device, sector, cache_entry->data);
      else
        memset (cache_entry->data, 0, BLOCK_SECTOR_SIZE); 
    }
  
  ASSERT (cache_entry != NULL);
  cache_entry->is_in_second_chance = false;
  cache_entry->is_dirty = true;

  memcpy (cache_entry->data + offset, buffer, size);

  lock_release(&cache_lock);
}

void bc_flush_all (void)
{
 
  lock_acquire(&cache_lock);

  struct list_elem *e;
  int count = 0;
  for (e = list_begin (&cache); e != list_end (&cache); e = list_next (e))
    {
      struct buffer_cache_entry *entry;
      entry = list_entry (e, struct buffer_cache_entry, elem);
      count ++;
      if (entry->is_dirty)
        {
          bc_flush(entry);
        }
    }

  lock_release(&cache_lock);
}

/* Get a fresh entry to use, either via allocating or 
   eviction. The given entry will be added to the bottom
   of the cache list */
static struct buffer_cache_entry * bc_get_free_entry ()
{
  struct buffer_cache_entry *cache_entry;
  if(cache_count < MAX_CACHE_SECTORS)
    {/* ALLOCATE new cache entry */
      cache_entry = malloc (sizeof (struct buffer_cache_entry));
      if (cache_entry == NULL) PANIC ("No memory left");
      list_insert(list_end (&cache), &cache_entry->elem);
      cache_count++;
    }
  else
    {/* EVICT present cache entry */
      cache_entry = bc_evict();
      bc_move_to_bottom (cache_entry);
    }

  return cache_entry;
}

static struct buffer_cache_entry * bc_evict ()
{
  /* EVICTION
      To evict page, we use a slightly modified version of the clock algorithm,
      in which we always start from the top of the list, and we cycle looking 
      for an entry in second chance. Since we always start from the beginning,
      but newly added entries are moved to the end, we have an implicit 
      mechanism of aging going on. On top of this, during the first iteration,
      the dirty entries will be ignored. This is done because evicting a dirty
      entry is more expensive.
  */
  struct buffer_cache_entry *victim = NULL;
  int round = 0;
  struct list_elem *e;
  for (e = list_begin (&cache); e != list_end (&cache); e = list_next (e))
  {
    struct buffer_cache_entry *entry;
    entry = list_entry (e, struct buffer_cache_entry, elem);
    if(!entry->is_in_second_chance)
    {
      if (!entry->is_dirty || round > 0)
        entry->is_in_second_chance = true;
    }
    else
    {
      victim = entry;
      break;
    }

    if(list_next (e) == list_end (&cache))
    {
      e = list_begin (&cache);
      round ++;
      ASSERT (round < 3);
    }
  }

  ASSERT (victim != NULL);
  if(victim->is_dirty)
    bc_flush (victim);

  return victim;
}

/* Move the element to the end of the cache list, to give
   it better chance of surviving an eviction */
static void bc_move_to_bottom (struct buffer_cache_entry *entry)
{
  list_remove(&entry->elem);
  list_insert(list_end (&cache), &entry->elem);
}

static void bc_flush (struct buffer_cache_entry *entry)
{
  block_write (fs_device, entry->sector, entry->data);
  entry->is_dirty = false;
}

void bc_remove (block_sector_t sector)
{
  struct list_elem *e;
  for (e = list_begin (&cache); e != list_end (&cache); e = list_next (e))
  {
    struct buffer_cache_entry *entry;
    entry = list_entry (e, struct buffer_cache_entry, elem);
    
    if (entry->sector == sector)
    {
      list_remove (&entry->elem);
      cache_count --;
      return;
    }  
  }
}

static struct buffer_cache_entry *bc_get_entry_by_sector (block_sector_t sector)
{
  struct list_elem *e;
  struct buffer_cache_entry *entry;
  for (e = list_begin (&cache); e != list_end (&cache); e = list_next (e))
    {
      entry = list_entry (e, struct buffer_cache_entry, elem);
      if(entry->sector == sector)
        return entry;
    }

  return NULL;
}

static void bf_daemon(void *aux UNUSED)
{ 
  while (true)
    {
      lock_acquire(&cache_lock);
      struct list_elem *e;
      struct buffer_cache_entry *entry;
      for (e = list_begin (&cache); e != list_end (&cache); e = list_next (e))
        {
          entry = list_entry (e, struct buffer_cache_entry, elem);
          if(entry->is_dirty)
            bc_flush (entry);
        }
      lock_release(&cache_lock);
      timer_msleep (BF_DAEMON_FLUSH_SLEEP_MS);
    }
}
