#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "filesys/cache.h"
#include "threads/malloc.h"

//allocate block sector or normal sector
#define CHECK_ALLOCATE_AND_GET_SECTOR(table, idx, is_index_block)\
({\
    if (allocate_new && is_index_block)\
      allocate_new_index_inode (table, idx);\
    if (allocate_new && !is_index_block)\
      allocate_new_block (table, idx);\
    ASSERT (table[idx] != SECTOR_ERROR);\
    block_sector_t ret = table[idx];\
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
    sector = lookup_real_sector_in_inode (inode, pos, false);

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
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      if (is_index_block)
        {
          memset (disk_inode->index.block_index, SECTOR_ERROR, INDEX_BLOCK_ENTRIES*sizeof (uint32_t));
          disk_inode->is_index_block = (uint32_t)is_index_block;
          disk_inode->magic = INODE_MAGIC;
          bc_block_write (sector, disk_inode, 0, BLOCK_SECTOR_SIZE);
          success = true;
        }
      else
        {
          size_t sectors = bytes_to_sectors (length);
          disk_inode->length = length;
          disk_inode->parent = parent; 
          memset (disk_inode->index.main_index, SECTOR_ERROR, INDEX_MAIN_ENTRIES*sizeof (uint32_t));
          disk_inode->is_index_block = (uint32_t)is_index_block;
          disk_inode->magic = INODE_MAGIC;
          if (free_map_allocate (sectors, &disk_inode->start)) 
            {
              bc_block_write (sector, disk_inode, 0, BLOCK_SECTOR_SIZE);
              if (sectors > 0) 
                {
                  static char zeros[BLOCK_SECTOR_SIZE];
                  size_t i;
                  
                  for (i = 0; i < sectors; i++) 
                    bc_block_write (disk_inode->start + i, zeros, 0, BLOCK_SECTOR_SIZE);
                }
              success = true; 
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
  inode->data = NULL; //Lazy loaded
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
  if (inode->data == NULL)
    {
      inode->data = malloc (sizeof (struct inode_disk));
      if (inode->data == NULL) 
        PANIC ("No memory left");
      bc_block_read (inode->sector, inode->data, 0, BLOCK_SECTOR_SIZE);
    }
}

void 
inode_release_disk (struct inode *inode)
{
  if (inode->data != NULL)
    {
      bc_block_write (inode->sector, inode->data, 0, BLOCK_SECTOR_SIZE);
      free(inode->data);
      inode->data = NULL;
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
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
  inode_load_disk (inode);

  uint8_t *buffer = (uint8_t *)buffer_;
  off_t bytes_read = 0;

  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
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
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{ //TODO implement file growth
  inode_load_disk (inode);

  uint8_t *buffer = (uint8_t *)buffer_;
  off_t bytes_written = 0;

  if (inode->deny_write_cnt)
    return 0;

  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      if (sector_idx == SECTOR_ERROR) // Read past end of inode
        {
          inode_grow (inode, size, offset);
          sector_idx = byte_to_sector (inode, offset);
        }

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      bc_block_write (sector_idx, buffer + bytes_written,
                      sector_ofs, chunk_size);

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }

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
  return inode->data->length;
  inode_release_disk (inode);
}

bool inode_grow (struct inode *inode, off_t size, off_t offset)
{
  ASSERT (offset >= inode->data->length);
  block_sector_t eof_sector = inode->data->start + bytes_to_sectors (inode->data->length) - 1;
  size_t gap = bytes_to_sectors (offset) - eof_sector - 1;
  size_t grow_len = gap + bytes_to_sectors (size);

  for (size_t i = 1; i <= grow_len; i++)
    if(write_create_sector(inode, eof_sector + i) == SECTOR_ERROR)
      return false;

  inode->data->length = inode->data->length + sectors_to_bytes (grow_len);
  return true;
}

block_sector_t write_create_sector (struct inode *inode, block_sector_t sector)
{
  block_sector_t sector_inode_relative = sector - inode->data->start;
  return lookup_real_sector_in_inode (inode, sectors_to_bytes (sector_inode_relative), true);
}

block_sector_t lookup_real_sector_in_inode (const struct inode *inode, off_t pos, bool allocate_new)
{
  ASSERT (inode != NULL);
  block_sector_t sector = SECTOR_ERROR;
  struct inode *index_inode, *d_index_inode;
  block_sector_t main_idx, inner_idx, d_inner_idx, offset_mult_64, offset_mult_4096, start;
  block_sector_t n, d;

  block_sector_t sector_inode_relative = pos / BLOCK_SECTOR_SIZE;

  if (sector_inode_relative <= INODE_ACCESS_DIRECT)
    {/* Normal lookup */
      sector = CHECK_ALLOCATE_AND_GET_SECTOR (inode->data->index.main_index, sector_inode_relative, false);
    }
  else if (INODE_ACCESS_DIRECT + 1 <= sector_inode_relative && 
          sector_inode_relative <= INODE_ACCESS_INDIRECT)
    { /* Lookup in indirect tables */
      // Normalize to [INODE_ACCESS_DIRECT+1 .. INODE_ACCESS_INDIRECT]
      n = sector_inode_relative - (INODE_ACCESS_DIRECT);
      d = (INODE_ACCESS_INDIRECT) - (INODE_ACCESS_DIRECT);
      main_idx = ROUND_UP (INODE_ACCESS_DIRECT + ((n / d) * INDIRECT_BLOCKS), 1);

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
      n = sector_inode_relative - (INODE_ACCESS_INDIRECT);
      d = (INODE_ACCESS_MAX) - (INODE_ACCESS_INDIRECT);
      main_idx = ROUND_UP (INODE_ACCESS_INDIRECT + ((n / d) * D_INDIRECT_BLOCKS), 1);

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

void allocate_new_block (block_sector_t *table, block_sector_t idx)
{
  static char zeros[BLOCK_SECTOR_SIZE];
  block_sector_t allocated_sector = 0;
  if (!free_map_allocate (1, &allocated_sector))
    PANIC ("Out of memory!");
  bc_block_write (allocated_sector, zeros, 0, BLOCK_SECTOR_SIZE);
  table[idx] = allocated_sector;
}

void allocate_new_index_inode (block_sector_t *table, block_sector_t idx)
{
  block_sector_t allocated_sector = 0;
  if (!free_map_allocate (1, &allocated_sector))
    PANIC ("Out of memory!");
  inode_create (allocated_sector, 0, 0, true);
  table[idx] = allocated_sector;
}

