#include "filesys/filesys.h"
#include "threads/malloc.h"
#include "lib/debug.h"
#include "lib/string.h"
#include "threads/thread.h"
#include "devices/timer.h"
#include "cache.h"

#define ENABLE_BUFFER_CACHE

#ifdef ENABLE_BUFFER_CACHE
  #define ENABLE_READ_AHEAD 
  #define ENABLE_PERIODIC_FLUSH 
#endif

static void bc_flush (struct buffer_cache_entry *entry);
bool bc_get_and_lock_entry (struct buffer_cache_entry **ref_entry, block_sector_t sector);
static struct buffer_cache_entry * bc_get_entry_by_sector (block_sector_t sector);
static struct buffer_cache_entry * bc_get_free_entry (void);

#ifdef ENABLE_PERIODIC_FLUSH
static void bc_daemon_flush(void *aux);
#endif

#ifdef ENABLE_READ_AHEAD
static void bc_daemon_read_ahead(void *aux);
#endif

struct buffer_cache_entry cache [MAX_CACHE_SECTORS];
struct lock cache_lock;
block_sector_t read_ahead [MAX_READ_AHEAD];
bool daemon_started;
struct semaphore rh_sema;

void bc_init ()
{
#ifdef ENABLE_BUFFER_CACHE
  for (int i = 0; i < MAX_CACHE_SECTORS; i++)
  {
    struct buffer_cache_entry *entry = &cache[i];
    entry->sector = EMPTY_SECTOR;
    entry->is_in_second_chance = false;
    entry->is_dirty = false;
    entry->readers = 0;
    lock_init (&cache[i].elock);
  }
#endif

  lock_init(&cache_lock);
  sema_init(&rh_sema, 0);
  daemon_started = false;
}

void bc_start_daemon ()
{
  ASSERT (!daemon_started);

#ifdef ENABLE_PERIODIC_FLUSH 
  tid_t t = thread_create("bc flush daemon", PRI_DEFAULT, bc_daemon_flush, NULL);
  ASSERT (t != TID_ERROR);
#endif

#ifdef ENABLE_READ_AHEAD
  t = thread_create("bc read ahead daemon", PRI_DEFAULT, bc_daemon_read_ahead, NULL);
  ASSERT (t != TID_ERROR);
#endif

  daemon_started = true;
}

void bc_block_read (block_sector_t sector, void *buffer, off_t offset, off_t size)
{
  ASSERT (offset + size <= BLOCK_SECTOR_SIZE);

#ifdef ENABLE_BUFFER_CACHE
  struct buffer_cache_entry *cache_entry = NULL;
  bool is_cache_miss = bc_get_and_lock_entry (&cache_entry, sector); //acquires elock

  lock_acquire (&cache_lock); 
  if(is_cache_miss)
      block_read (fs_device, sector, cache_entry->data);
  lock_release (&cache_lock);

  cache_entry->is_in_second_chance = false;
  cache_entry->readers ++;
  lock_release(&cache_entry->elock);
  
  memcpy (buffer, cache_entry->data + offset, size);

  lock_acquire(&cache_entry->elock);
  cache_entry->readers --; //entry will not be evicted, readers > 0
  lock_release(&cache_entry->elock);
#else
  lock_acquire(&cache_lock);
  uint8_t *bounce = malloc (BLOCK_SECTOR_SIZE);
  if (bounce == NULL)
    PANIC ("Malloc failed!");
  
  block_read (fs_device, sector, bounce);
  memcpy (buffer, bounce + offset, size);
  free (bounce);
  lock_release(&cache_lock);
#endif
}

/* Guarantees to find and return an entry allocated for the given sector.
   Returns true in case of cache HIT, false otherwise. If the return is 
   false, the data field will not be valid. Call with cache lock DISABLED
*/
bool bc_get_and_lock_entry (struct buffer_cache_entry **ref_entry, block_sector_t sector)
{
  bool found = false;
  bool is_cache_miss;
  struct buffer_cache_entry *e;

  do
  {
    lock_acquire (&cache_lock);
    e = bc_get_entry_by_sector(sector);

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
    
    ASSERT (lock_held_by_current_thread(&e->elock));

    found = sector == e->sector;

    if (!found)
      lock_release (&e->elock);

    lock_release (&cache_lock);

  } while (!found);

  ASSERT (e != NULL);
  ASSERT (e->sector == sector);
  ASSERT (lock_held_by_current_thread(&e->elock));

  *ref_entry = e;
  return is_cache_miss;
}

void bc_request_read_ahead (block_sector_t sector UNUSED /*when RH disabled*/)
{
#ifdef ENABLE_READ_AHEAD
  for (int i = 0; i < MAX_READ_AHEAD; i++)
    {
      if (read_ahead[i] != EMPTY_SECTOR)
      {
        read_ahead[i] = sector;
        sema_up (&rh_sema);
        return;
      }
    }
#endif
}

void bc_block_write (block_sector_t sector, void *buffer, off_t offset, off_t size)
{
#ifdef ENABLE_BUFFER_CACHE
  ASSERT (offset + size <= BLOCK_SECTOR_SIZE);

  struct buffer_cache_entry *cache_entry = NULL;
  bool is_allowed;
  bool is_cache_miss;

  while (!is_allowed)
  {
    is_cache_miss = bc_get_and_lock_entry (&cache_entry, sector); //acquires elock

    if(is_cache_miss)
    {
      if (offset > 0 || 
          size + offset < BLOCK_SECTOR_SIZE) 
          block_read (fs_device, sector, cache_entry->data);
        else
          memset (cache_entry->data, 0, BLOCK_SECTOR_SIZE);
    }

    is_allowed = cache_entry->readers == 0;

    if (!is_allowed)
      lock_release(&cache_entry->elock);
  }

  cache_entry->is_in_second_chance = false;
  cache_entry->is_dirty = true;
  memcpy (cache_entry->data + offset, buffer, size);

  lock_release(&cache_entry->elock);
#else
  lock_acquire(&cache_lock);
  uint8_t *bounce = malloc (BLOCK_SECTOR_SIZE);
  if (bounce == NULL)
    PANIC ("Malloc failed!");

  if (offset > 0 || size + offset < BLOCK_SECTOR_SIZE)
    block_read (fs_device, sector, bounce);
  else
    memset (bounce, 0, BLOCK_SECTOR_SIZE);

  memcpy (bounce + offset, buffer, size);
  block_write (fs_device, sector, bounce);
  free (bounce);
  lock_release(&cache_lock);
#endif
}

void bc_flush_all (void)
{
#ifdef ENABLE_BUFFER_CACHE
  lock_acquire(&cache_lock);
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
  lock_release(&cache_lock);
#endif
}

/* Get a fresh entry to use, either via allocating or 
   eviction. Call with cache lock ENABLED.
   The returned entry will be locked by the current thread */
static struct buffer_cache_entry * bc_get_free_entry ()
{
  struct buffer_cache_entry *victim = NULL;
  int round = 0;
  for (int i = 0; i < MAX_CACHE_SECTORS;)
  {
    bool allow_halt = round > 5;

    struct buffer_cache_entry *entry = &cache[i];

    bool taken;
    if(allow_halt)
    {
      lock_acquire (&entry->elock);
      taken = true;
    }
    else
      taken = lock_try_acquire (&entry->elock);

    if(taken)
    {
      bool readers_present = entry->readers > 0;

      if (!readers_present)
      {
        if(entry->sector == EMPTY_SECTOR || 
           entry->is_in_second_chance)
          {
            victim = entry;
            break;
          }
        else if (!entry->is_dirty || round > 0)
            entry->is_in_second_chance = true;
      }

      lock_release (&entry->elock);
    }

    if (i == MAX_CACHE_SECTORS - 1)
    {
      i = 0;
      round ++;
      //ASSERT (round < 3);
    }
    else
      i++;
  }


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

void bc_remove (block_sector_t sector UNUSED)
{
#ifdef ENABLE_BUFFER_CACHE
  lock_acquire(&cache_lock);
  for (int i = 0; i < MAX_CACHE_SECTORS; i++)
  {
    struct buffer_cache_entry *entry = &cache[i];
    if (entry->sector == sector)
    {
      lock_acquire (&entry->elock);
      if (entry->sector == sector) //double check for eviction
        entry->sector = EMPTY_SECTOR;
      lock_release (&entry->elock);
      lock_release(&cache_lock);
      return;
    }  
  }
  lock_release(&cache_lock);
#endif
}

/* Call with cache lock ENABLED */
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


#ifdef ENABLE_PERIODIC_FLUSH 
static void bc_daemon_flush(void *aux UNUSED)
{ 
  while (true)
    {
      lock_acquire(&cache_lock);
      struct buffer_cache_entry *entry;
      for (int i = 0; i < MAX_CACHE_SECTORS; i++)
        {
          entry = &cache[i];
          lock_acquire (&entry->elock);
          if(entry->is_dirty)
            bc_flush (entry);
          lock_release (&entry->elock);
        }

      lock_release(&cache_lock);

      timer_msleep (BC_DAEMON_FLUSH_SLEEP_MS);
    }
}
#endif

#ifdef ENABLE_READ_AHEAD
static void bc_daemon_read_ahead(void *aux UNUSED)
{
  while (true)
    {
      sema_down (&rh_sema);
      //lock_acquire(&cache_lock);
      for (int i = 0; i < MAX_READ_AHEAD; i++)
        {
          block_sector_t sector = read_ahead [i];
          if(sector != EMPTY_SECTOR)
            {
              struct buffer_cache_entry *cache_entry = NULL;
              bool is_cache_miss = bc_get_and_lock_entry (&cache_entry, sector); //acquires elock
 
              lock_acquire(&cache_lock);
              if(is_cache_miss)
                  block_read (fs_device, sector, cache_entry->data);
              lock_release(&cache_lock);

              cache_entry->is_in_second_chance = false;
              lock_release (&cache_entry->elock);

              read_ahead [i] = EMPTY_SECTOR;
            }  
        }
      //lock_release(&cache_lock);
    }
}
#endif