#include "filesys/filesys.h"
#include "threads/malloc.h"
#include "lib/debug.h"
#include "lib/string.h"
#include "threads/thread.h"
#include "devices/timer.h"
#include "cache.h"

static void bc_flush (struct buffer_cache_entry *entry);
bool bc_get_and_lock_entry (struct buffer_cache_entry **ref_entry, block_sector_t sector);
static struct buffer_cache_entry * bc_get_entry_by_sector (block_sector_t sector);
static struct buffer_cache_entry * bc_get_free_entry (void);
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
  {
    struct buffer_cache_entry *entry = &cache[i];
    entry->sector = EMPTY_SECTOR;
    entry->is_in_second_chance = false;
    entry->is_dirty = false;
    entry->readers = 0;
    lock_init (&cache[i].elock);
  }

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

  struct buffer_cache_entry *cache_entry = NULL;
  bool is_cache_miss = bc_get_and_lock_entry (&cache_entry, sector);

  if(is_cache_miss)
      block_read (fs_device, sector, cache_entry->data);

  cache_entry->is_in_second_chance = false;
  cache_entry->readers ++;
  lock_release(&cache_entry->elock);
  
  memcpy (buffer, cache_entry->data + offset, size);

  lock_acquire(&cache_entry->elock);
  cache_entry->readers --; //entry will not be evicted, readers > 0
  lock_release(&cache_entry->elock);
}

/* Guarantees to find and return an entry allocated for the given sector.
   Returns true in case of cache HIT, false otherwise. If the return is 
   false, the data field will not be valid.
*/
bool bc_get_and_lock_entry (struct buffer_cache_entry **ref_entry, block_sector_t sector)
{
  bool is_cache_miss;
  struct buffer_cache_entry *e;

  do
  {
    lock_acquire (&cache_lock);
    e = bc_get_entry_by_sector(sector);
    lock_release (&cache_lock);

    if (e == NULL)
      { /* CACHE MISS */
        is_cache_miss = true;
        e = bc_get_free_entry (); //will acquire elock
        e->sector = sector;
        e->is_dirty = false;
      }
    else
      { /* CACHE HIT */
        is_cache_miss = false;
        lock_acquire (&e->elock);
      }
  } while (sector != e->sector);

  ASSERT (e != NULL);
  ASSERT (e->sector == sector);
  ASSERT (lock_held_by_current_thread(&e->elock));

  *ref_entry = e;
  return is_cache_miss;
}

void bc_request_read_ahead (block_sector_t sector)
{
  for (int i = 0; i < MAX_READ_AHEAD; i++)
    {
      if (read_ahead[i] != EMPTY_SECTOR)
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

  struct buffer_cache_entry *cache_entry = NULL;
  bool is_cache_miss = bc_get_and_lock_entry (&cache_entry, sector);

  if(is_cache_miss)
  {
    if (offset > 0 || 
          size + offset < BLOCK_SECTOR_SIZE) 
        block_read (fs_device, sector, cache_entry->data);
      else
        memset (cache_entry->data, 0, BLOCK_SECTOR_SIZE);
  }

  cache_entry->is_in_second_chance = false;
  cache_entry->is_dirty = true;
  memcpy (cache_entry->data + offset, buffer, size);

  lock_release(&cache_entry->elock);
}

void bc_flush_all (void)
{
  int count = 0;
  for (int i = 0; i < MAX_CACHE_SECTORS; i++)
    {
      struct buffer_cache_entry *entry = &cache[i];
      lock_acquire (&entry->elock);
      count ++;
      if (entry->sector != EMPTY_SECTOR && entry->is_dirty)
        {
          bc_flush(entry);
        }
      lock_release (&entry->elock);
    }
}

/* Get a fresh entry to use, either via allocating or 
   eviction. Call with cache lock DISABLED.
   The returned entry will be locked by the current thread */
static struct buffer_cache_entry * bc_get_free_entry ()
{
  /* EVICTION
  TODO UPDATE
      To evict page, we use a slightly modified version of the clock algorithm,
      in which we always start from the top of the array, and we cycle looking 
      for an entry in second chance. Since we always start from the beginning,
      but newly added entries are moved to the end, we have an implicit 
      mechanism of aging going on. On top of this, during the first iteration,
      the dirty entries will be ignored. This is done because evicting a dirty
      entry is more expensive.
  */
  lock_acquire (&cache_lock);
  struct buffer_cache_entry *victim = NULL;
  int round = 0;
  for (int i = 0; i < MAX_CACHE_SECTORS;)
  {
    bool allow_halt = round > 1;

    struct buffer_cache_entry *entry = &cache[i];
    bool readers_present = entry->readers > 0;

    if(!readers_present)
    {
      if(entry->sector == EMPTY_SECTOR || 
         entry->is_in_second_chance)
      {
        bool get_victim;
        if (allow_halt)
        {//Eviction is taking a long time, wait on the lock
          lock_acquire (&entry->elock);
          get_victim = true;
        }
        else
        {
          get_victim = lock_try_acquire (&entry->elock);
        }

        if(get_victim)
        {
          victim = entry;
          break;
        }
      }
      else
      {
        if (!entry->is_dirty || round > 0)
          entry->is_in_second_chance = true;
      }
    }

    if (i == MAX_CACHE_SECTORS - 1)
    {
      i = 0;
      round ++;
      ASSERT (round < 3);
    }
    else
      i++;
  }

  lock_release (&cache_lock);

  ASSERT (victim != NULL);
  ASSERT (lock_held_by_current_thread(&victim->elock))
  
  if(victim->sector != EMPTY_SECTOR && victim->is_dirty)
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
      lock_acquire (&entry->elock);
      if (entry->sector == sector) //double check for eviction
      {
        entry->sector = EMPTY_SECTOR;
        cache_count --;
      }
      lock_release (&entry->elock);
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
              struct buffer_cache_entry *cache_entry = NULL;
              bool is_cache_miss = bc_get_and_lock_entry (&cache_entry, sector);

              if(is_cache_miss)
                  block_read (fs_device, sector, cache_entry->data);

              cache_entry->is_in_second_chance = false;
              lock_release(&cache_entry->elock);

              read_ahead [i] = EMPTY_SECTOR;
            }  
        }
      lock_release(&cache_lock);
    }
}