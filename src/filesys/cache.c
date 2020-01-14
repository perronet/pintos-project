#include "filesys/filesys.h"
#include "threads/malloc.h"
#include "lib/debug.h"
#include "lib/string.h"
#include "threads/thread.h"
#include "devices/timer.h"
#include "cache.h"

static void bc_flush (struct buffer_cache_entry *entry);
static struct buffer_cache_entry * bc_get_entry_by_sector (block_sector_t sector);
static struct buffer_cache_entry * bc_get_free_entry (void);
static struct buffer_cache_entry * bc_evict (void);
static void bc_daemon_flush(void *aux);
static void bc_daemon_read_ahead(void *aux);

struct buffer_cache_entry cache [MAX_CACHE_SECTORS];
struct lock cache_lock;
block_sector_t read_ahead [MAX_READ_AHEAD];
int cache_count;
bool daemon_started;
struct semaphore rh_sema;

void bc_init ()
{
  for (int i = 0; i < MAX_CACHE_SECTORS; i++)
    cache[i].sector = 0;

  lock_init(&cache_lock);
  sema_init(&rh_sema, 0);
  daemon_started = false;

  cache_count = 0;
}

void bc_start_daemon ()
{
  ASSERT (!daemon_started);
  tid_t t = thread_create("bc flush daemon", PRI_DEFAULT, bc_daemon_flush, NULL);
  ASSERT (t != TID_ERROR);
  t = thread_create("bc read ahead daemon", PRI_DEFAULT, bc_daemon_read_ahead, NULL);
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
      //TODO Record some info?
    }
  else
    {/* CACHE MISS */
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

void bc_request_read_ahead (block_sector_t sector)
{
  for (int i = 0; i < MAX_READ_AHEAD; i++)
    {
      if (read_ahead[i] != 0)
      {
        read_ahead[i] = sector;
        sema_up (&rh_sema);
        return;
      }
    }
}

void bc_block_write (block_sector_t sector, void *buffer, off_t offset, off_t size)
{
  ASSERT (offset + size <= BLOCK_SECTOR_SIZE);

  lock_acquire(&cache_lock);

  struct buffer_cache_entry *cache_entry = bc_get_entry_by_sector(sector);

  if (cache_entry != NULL)
    {/* CACHE HIT */
      //TODO Record some info?
    }
  else
    {/* CACHE MISS */
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

  int count = 0;
  for (int i = 0; i < MAX_CACHE_SECTORS; i++)
    {
      struct buffer_cache_entry *entry = &cache[i];
      count ++;
      if (entry->is_dirty)
        {
          bc_flush(entry);
        }
    }

  lock_release(&cache_lock);
}

/* Get a fresh entry to use, either via allocating or 
   eviction. */
static struct buffer_cache_entry * bc_get_free_entry ()
{
  struct buffer_cache_entry *cache_entry = NULL;
  if(cache_count < MAX_CACHE_SECTORS)
    {/* ALLOCATE new cache entry */
      for (int i = 0; i < MAX_CACHE_SECTORS; i++)
        {
          if (cache[i].sector == 0)
          {
            cache_entry = &cache[i];
            lock_init (&cache_entry->entry_lock);
            cond_init (&cache_entry->entry_cond);
            break;
          }
        }
      cache_count++;
    }
  else
    {/* EVICT present cache entry */
      cache_entry = bc_evict();
    }

  return cache_entry;
}

static struct buffer_cache_entry * bc_evict ()
{
  /* EVICTION
      To evict page, we use a slightly modified version of the clock algorithm,
      in which we always start from the top of the array, and we cycle looking 
      for an entry in second chance. Since we always start from the beginning,
      but newly added entries are moved to the end, we have an implicit 
      mechanism of aging going on. On top of this, during the first iteration,
      the dirty entries will be ignored. This is done because evicting a dirty
      entry is more expensive.
  */
  struct buffer_cache_entry *victim = NULL;
  int round = 0;
  for (int i = 0; i < MAX_CACHE_SECTORS; i++)
  {
    struct buffer_cache_entry *entry;
    entry = &cache[i];
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

    if (i == MAX_CACHE_SECTORS - 1)
    {
      i = 0;
      round ++;
      ASSERT (round < 3);
    }
  }

  ASSERT (victim != NULL);
  if(victim->is_dirty)
    bc_flush (victim);

  return victim;
}

static void bc_flush (struct buffer_cache_entry *entry)
{
  block_write (fs_device, entry->sector, entry->data);
  entry->is_dirty = false;
}

void bc_remove (block_sector_t sector)
{
  for (int i = 0; i < MAX_CACHE_SECTORS; i++)
  {
    struct buffer_cache_entry *entry = &cache[i];
    if (entry->sector == sector)
    {
      entry->sector = 0;
      cache_count --;
      return;
    }  
  }
}

static struct buffer_cache_entry *bc_get_entry_by_sector (block_sector_t sector)
{
  struct buffer_cache_entry *entry;
  for (int i = 0; i < MAX_CACHE_SECTORS; i++)
    {
      entry = &cache[i];
      if(entry->sector == sector)
        return entry;
    }

  return NULL;
}

static void bc_daemon_flush(void *aux UNUSED)
{ 
  while (true)
    {
      lock_acquire(&cache_lock);
      struct buffer_cache_entry *entry;
      for (int i = 0; i < MAX_CACHE_SECTORS; i++)
        {
          entry = &cache[i];
          if(entry->is_dirty)
            bc_flush (entry);
        }

      lock_release(&cache_lock);

      timer_msleep (BC_DAEMON_FLUSH_SLEEP_MS);
    }
}

static void bc_daemon_read_ahead(void *aux UNUSED)
{
  while (true)
    {
      sema_down (&rh_sema);
      lock_acquire(&cache_lock);
      for (int i = 0; i < MAX_READ_AHEAD; i++)
        {
          block_sector_t sector = read_ahead [i];
          if(sector != 0)
            {
              #ifdef CHECK_READ_AHEAD
              struct buffer_cache_entry *cache_entry;
              cache_entry = bc_get_entry_by_sector(sector);
              ASSERT (cache_entry == NULL);
              #endif

              cache_entry = bc_get_free_entry ();
              cache_entry->sector = sector;
              cache_entry->is_dirty = false;
              block_read (fs_device, sector, cache_entry->data);
              read_ahead [i] = 0;
            }  
        }
      lock_release(&cache_lock);
    }
}