#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include <stdio.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "filesys/cache.h"
#include "threads/malloc.h"

//allocate block sector or normal sector
#define CHECK_ALLOCATE_AND_GET_SECTOR(table, idx, is_index_block)\
({\
    if (allocate_new && table[idx] == SECTOR_ERROR)\
      {\
        if (is_index_block)\
          allocated_sector = allocate_new_index_inode (table, idx);\
        else\
          allocated_sector = allocate_new_block (table, idx);\
        if (allocated_sector == SECTOR_ERROR)\
          return SECTOR_ERROR;\
      }\
    block_sector_t ret = table[idx];\
    if (!allocate_new && ret == SECTOR_ERROR)\
      return SECTOR_ERROR;\
    ret;\
})

static void inode_release_disk (struct inode *inode);
static void inode_load_disk (struct inode *inode);

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
sectors_to_bytes (block_sector_t size)
{
  return size*BLOCK_SECTOR_SIZE;
}

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns SECTOR_ERROR if INODE does not contain data for a byte at offset
   POS. 
   Remember to call this after having loaded the inode in memory.
   */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos) 
{
  ASSERT (inode != NULL);
  block_sector_t sector = SECTOR_ERROR;

  if (pos < inode->data->length)
    sector = inode_pos_to_real_sector (inode, pos, false);

  return sector;
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length, block_sector_t parent, bool is_index_block)
{
  struct inode_disk *disk_inode = NULL;
  struct inode *inode = NULL;
  bool success = true;
  block_sector_t start_block;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      if (is_index_block)
        {
          for (int i = 0; i < INDEX_BLOCK_ENTRIES; i++)
            disk_inode->index.block_index[i] = SECTOR_ERROR;
          disk_inode->is_index_block = (uint32_t)is_index_block;
          disk_inode->magic = INODE_MAGIC;
          // Write block for inode
          bc_block_write (sector, disk_inode, 0, BLOCK_SECTOR_SIZE);
        }
      else
        {
          // Nothing is allocated yet: allocate first sector to have a starting position
          start_block = allocate_new_block (disk_inode->index.main_index, 0);
          if(start_block == SECTOR_ERROR)
            return false;
          disk_inode->start = start_block;
          // All other blocks are empty
          for (int i = 1; i < INDEX_MAIN_ENTRIES; i++)
            disk_inode->index.main_index[i] = SECTOR_ERROR;

          // Set length based on the fact that the first block is allocated
          if (length >= BLOCK_SECTOR_SIZE)
            disk_inode->length = BLOCK_SECTOR_SIZE;
          else
            disk_inode->length = length;

          disk_inode->parent = parent; 
          disk_inode->is_index_block = (uint32_t)is_index_block;
          disk_inode->magic = INODE_MAGIC;

          // Write block for inode
          bc_block_write (sector, disk_inode, 0, BLOCK_SECTOR_SIZE);

          // If bigger than one block, grow inode to desired initial size (Allocates non-contiguously)
          if (length > BLOCK_SECTOR_SIZE)
            {
              inode = inode_open (sector);
              inode_load_disk (inode);
              success = inode_grow (inode, length, 0);
              inode_release_disk (inode);
              inode_close (inode);
            }
        }
      free (disk_inode);
    }
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          inode_reopen (inode);
          return inode; 
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->open_fd_cnt = 0;
  inode->cwd_cnt = 0;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  lock_init (&inode->inode_lock);
  lock_init (&inode->inode_growth);
  DEBUG_LOCK_ID (&inode->inode_lock, 15043);
  inode->data = NULL; //Lazy loaded
  inode->access_count = 0;
  inode->logical_length = -1;
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

block_sector_t 
inode_get_parent (struct inode *inode)
{
  ASSERT (inode != NULL);
  inode_load_disk (inode);
  block_sector_t result = inode->data->parent;
  inode_release_disk (inode);

  return result;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);
 
      /* Deallocate blocks if removed. */
      if (inode->removed) 
        {
          free_map_release (inode->sector, 1);
          inode_load_disk (inode);
          free_map_release (inode->data->start,
                            bytes_to_sectors (inode->data->length)); 
          inode_release_disk (inode);
        }

      free (inode); 
    }
}

void
inode_load_disk (struct inode *inode)
{
  lock_acquire (&inode->inode_lock);
  ASSERT (inode->access_count != 0 || inode->data == NULL);
  if (inode->access_count == 0) //Don't re-load
    {
      ASSERT (inode->data == NULL);
      inode->data = malloc (sizeof (struct inode_disk));
      if (inode->data == NULL) 
        PANIC ("No memory left");
      bc_block_read (inode->sector, inode->data, 0, BLOCK_SECTOR_SIZE);

      if(inode->logical_length == -1)
        inode->logical_length = inode->data->length;
    }

  inode->access_count ++;
  lock_release (&inode->inode_lock);
}

void 
inode_release_disk (struct inode *inode)
{
  lock_acquire (&inode->inode_lock);
  ASSERT (inode->access_count > 0);
  ASSERT (inode->access_count != 0 || inode->data == NULL);
  inode->access_count --; 
  if (inode->access_count == 0)
    {
      bc_block_write (inode->sector, inode->data, 0, BLOCK_SECTOR_SIZE);
      free(inode->data);
      inode->data = NULL;
    }
  lock_release (&inode->inode_lock);
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) //TODO maybe also remove all inodes pointed by the main index?
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  if (inode->logical_length == 0)
    return 0;

  inode_load_disk (inode);

  uint8_t *buffer = (uint8_t *)buffer_;
  off_t bytes_read = 0;

  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      if (sector_idx == SECTOR_ERROR)
        break;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;
      off_t inode_left = inode->logical_length - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      bc_block_read (sector_idx, buffer + bytes_read, 
                     sector_ofs, chunk_size);
      
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }

  inode_release_disk (inode);
  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs. */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  inode_load_disk (inode);

  uint8_t *buffer = (uint8_t *)buffer_;
  off_t bytes_written = 0;

  bool is_growing;
  while (size > 0 && inode->deny_write_cnt == 0) 
    {
      is_growing = false;

      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      if (sector_idx == SECTOR_ERROR) // Read past end of inode
        {
          lock_acquire (&inode->inode_lock);
          is_growing = inode_grow (inode, size, offset);

          if (!is_growing)
          {
            lock_release (&inode->inode_lock);
            break;
          }
          sector_idx = byte_to_sector (inode, offset);
        }

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode->data->length - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
      {
        if (is_growing)
        {
          inode->logical_length = inode->data->length;
          lock_release (&inode->inode_lock);
        }
        break;
      }

      bc_block_write (sector_idx, buffer + bytes_written,
                      sector_ofs, chunk_size);

      if (is_growing)
      {
        inode->logical_length = inode->data->length;
        lock_release (&inode->inode_lock);
      }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }

  ASSERT (!lock_held_by_current_thread (&inode->inode_lock));
  ASSERT (!lock_held_by_current_thread (&inode->inode_growth)); 

  inode_release_disk (inode);
  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  ASSERT (inode != NULL);
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  ASSERT (inode != NULL);
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (struct inode *inode)
{
  inode_load_disk (inode);
  off_t r = inode->data->length;
  inode_release_disk (inode);
  return r;
}

// Assumes that the starting sector is already allocated
// Assumes that the byte "inode->data->length" is in the last sector of the inode;
bool inode_grow (struct inode *inode, off_t size, off_t offset)
{
  ASSERT (size > 0);
  block_sector_t sector_inode_relative;

  if (offset + size > round_up_to_sector_boundary (inode->data->length))
    { // Actually allocate more sectors
      block_sector_t last_sector;  
      off_t grow_len_bytes;
      size_t grow_len;
      if (inode->data->length <= BLOCK_SECTOR_SIZE)
        last_sector = inode->data->start;
      else
        last_sector = inode->data->start + bytes_to_sectors (inode->data->length) - 1;
      grow_len_bytes = offset + size - round_up_to_sector_boundary (inode->data->length);
      grow_len = bytes_to_sectors (grow_len_bytes);
      ASSERT (grow_len > 0);

      for (size_t i = 1; i <= grow_len; i++)
      {
        sector_inode_relative = last_sector + i - inode->data->start;
        if(inode_pos_to_real_sector (inode, sectors_to_bytes (sector_inode_relative), true) == SECTOR_ERROR)
          return false;
      }

      inode->data->length = inode->data->length + grow_len_bytes;
    }
  else
    { // Just grow inode length value
      inode->data->length = offset + size; 
    }

  return true;
}

/* Returns SECTOR_ERROR if the sector needs to be allocated */
block_sector_t inode_pos_to_real_sector (const struct inode *inode, off_t pos, bool allocate_new)
{
  ASSERT (inode != NULL);
  ASSERT (pos >= 0);
  block_sector_t sector = SECTOR_ERROR;
  struct inode *index_inode, *d_index_inode;
  block_sector_t main_idx, inner_idx, d_inner_idx, offset_mult_64, offset_mult_4096, start, allocated_sector;

  block_sector_t sector_inode_relative = pos / BLOCK_SECTOR_SIZE;

  if (sector_inode_relative <= INODE_ACCESS_DIRECT)
    { /* Normal lookup */
      sector = CHECK_ALLOCATE_AND_GET_SECTOR (inode->data->index.main_index, sector_inode_relative, false);
    }
  else if (INODE_ACCESS_DIRECT + 1 <= sector_inode_relative && 
          sector_inode_relative <= INODE_ACCESS_INDIRECT)
    { /* Lookup in indirect tables */
      // Normalize to [INODE_ACCESS_DIRECT+1 .. INODE_ACCESS_INDIRECT]
      main_idx = DIV_ROUND_UP (sector_inode_relative - INODE_ACCESS_DIRECT, INDEX_BLOCK_ENTRIES) + INODE_ACCESS_DIRECT;

      //Find index inode sector
      sector = CHECK_ALLOCATE_AND_GET_SECTOR (inode->data->index.main_index, main_idx, true); 

      index_inode = inode_open (sector);
      ASSERT (index_inode != NULL);
      inode_load_disk (index_inode);
      ASSERT (index_inode->data->is_index_block);

      start = DIRECT_BLOCKS;
      offset_mult_64 = (start + (INDEX_BLOCK_ENTRIES * (main_idx - start)));
      // [0..63]
      inner_idx = sector_inode_relative - offset_mult_64;

      sector = CHECK_ALLOCATE_AND_GET_SECTOR (index_inode->data->index.block_index, inner_idx, false);

      inode_release_disk (index_inode);
    }
  else if (INODE_ACCESS_INDIRECT+1 <= sector_inode_relative &&
          sector_inode_relative <= INODE_ACCESS_MAX)
    { /* Lookup in doubly indirect tables */
      // Normalize to [INODE_ACCESS_INDIRECT+1 .. INODE_ACCESS_MAX]
      main_idx = DIV_ROUND_UP (sector_inode_relative - INODE_ACCESS_INDIRECT, INDEX_BLOCK_ENTRIES*INDEX_BLOCK_ENTRIES) + INODE_ACCESS_INDIRECT;

      //Find index inode sector
      sector = CHECK_ALLOCATE_AND_GET_SECTOR (inode->data->index.main_index, main_idx, true);

      index_inode = inode_open (sector);
      ASSERT (index_inode != NULL);
      inode_load_disk (index_inode);
      ASSERT (index_inode->data->is_index_block);

      start = DIRECT_BLOCKS + INDIRECT_BLOCKS * INDEX_BLOCK_ENTRIES;
      offset_mult_4096 = (start + (INDEX_BLOCK_ENTRIES * INDEX_BLOCK_ENTRIES * (main_idx - start)));
      // [0..4097]
      inner_idx = sector_inode_relative - offset_mult_4096;
      
      //Find double index inode sector
      sector = CHECK_ALLOCATE_AND_GET_SECTOR (index_inode->data->index.block_index, inner_idx, true);

      d_index_inode = inode_open (sector);
      ASSERT (d_index_inode != NULL);
      inode_load_disk (d_index_inode);
      ASSERT (d_index_inode->data->is_index_block);

      start = DIRECT_BLOCKS + INDIRECT_BLOCKS * INDEX_BLOCK_ENTRIES;
      offset_mult_64 = (start + (INDEX_BLOCK_ENTRIES * (main_idx - start)));
      // [0..63]
      d_inner_idx = sector_inode_relative - offset_mult_64;

      sector = CHECK_ALLOCATE_AND_GET_SECTOR (d_index_inode->data->index.block_index, d_inner_idx, false);

      inode_release_disk (d_index_inode);
      inode_release_disk (index_inode);
    }
  else
    {
      PANIC ("OUT OF MEMORY: Trying to write to disk a file or folder greater than 8MB!");
    }

  return sector;
}

// Allocates block and sets entry in inode index
block_sector_t allocate_new_block (block_sector_t *table, block_sector_t idx)
{
  static char zeros[BLOCK_SECTOR_SIZE];
  block_sector_t allocated_sector = 0;
  if (!free_map_allocate (1, &allocated_sector))
    return SECTOR_ERROR;

  bc_block_write (allocated_sector, zeros, 0, BLOCK_SECTOR_SIZE);
  table[idx] = allocated_sector;

  return allocated_sector;
}

// Allocates index inode and sets entry in inode index
block_sector_t allocate_new_index_inode (block_sector_t *table, block_sector_t idx)
{
  block_sector_t allocated_sector = 0;
  if (!free_map_allocate (1, &allocated_sector))
    return SECTOR_ERROR;

  if (!inode_create (allocated_sector, 0, 0, true))
    return SECTOR_ERROR;
  table[idx] = allocated_sector;

  return allocated_sector;
}

off_t round_up_to_sector_boundary (off_t bytes)
{
  return sectors_to_bytes (bytes_to_sectors (bytes));
}