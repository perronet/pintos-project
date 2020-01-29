#ifndef FILESYS_INODE_H
#define FILESYS_INODE_H

#include <stdbool.h>
#include "filesys/off_t.h"
#include "devices/block.h"
#include "threads/synch.h"
#include "lib/kernel/list.h"

struct bitmap;

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44
#define DIRECT_BLOCKS 11
#define INDIRECT_BLOCKS 65
#define D_INDIRECT_BLOCKS 3
#define METADATA_BLOCKS (1 + INDIRECT_BLOCKS + D_INDIRECT_BLOCKS * D_INDIRECT_BLOCKS)
#define INDEX_MAIN_ENTRIES (DIRECT_BLOCKS + INDIRECT_BLOCKS + D_INDIRECT_BLOCKS)
#define INDEX_BLOCK_ENTRIES 64
#define UNUSED_SIZE (123-INDEX_MAIN_ENTRIES)
#define SECTOR_ERROR (6666666)

/* When accessing a sector number relative to an inode, each of these numbers 
   represent in which part of the table that sector should be looked for.
   Another way to think of these: they are indexes that split the table. */
#define INODE_ACCESS_DIRECT (DIRECT_BLOCKS - 1)
#define INODE_ACCESS_INDIRECT \
(INODE_ACCESS_DIRECT + (INDIRECT_BLOCKS * INDEX_BLOCK_ENTRIES)) // 4170
#define INODE_ACCESS_MAX \
(INODE_ACCESS_INDIRECT + \
(D_INDIRECT_BLOCKS * INDEX_BLOCK_ENTRIES * INDEX_BLOCK_ENTRIES) \
- METADATA_BLOCKS) // 16383

union index_table
  {
    block_sector_t main_index[INDEX_MAIN_ENTRIES];		/* Main index of next blocks */
    block_sector_t block_index[INDEX_BLOCK_ENTRIES];	/* Supplementary index if it is an index block */
  };

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    block_sector_t start;               	/* First data sector. */
    block_sector_t parent;								/* The directory containing the inode */
    off_t length;                       	/* File size in bytes. */
  	union index_table index;							/* Main index or supplementary index */
    uint32_t is_index_block;							/* Is it a normal data block or an index block? */
    unsigned magic;                     	/* Magic number. */
    uint32_t unused[UNUSED_SIZE];    			/* Not used. */
  };

/* In-memory inode. */
struct inode 
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    int open_fd_cnt;					          /* Number of open fds on this dir. */
    int cwd_cnt;                        /* Number of processes that have this dir as cwd. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct inode_disk* data;            /* Inode content. */
    struct lock inode_lock;             /* Used to synchronize file loading */
    struct lock inode_growth;           /* Used to synchronize file growth */
    int access_count;                   /* Number of threads currently using this inode */
    off_t logical_length;               /* Physical size minus what still needs to be initialized */
  };

void inode_init (void);
bool inode_create (block_sector_t sector, off_t length, block_sector_t parent, bool is_index_block);
struct inode *inode_open (block_sector_t);
struct inode *inode_reopen (struct inode *);
block_sector_t inode_get_inumber (const struct inode *);
block_sector_t inode_get_parent (struct inode *);
void inode_close (struct inode *);
void inode_remove (struct inode *);
off_t inode_read_at (struct inode *, void *, off_t size, off_t offset);
off_t inode_write_at (struct inode *, const void *, off_t size, off_t offset);
void inode_deny_write (struct inode *);
void inode_allow_write (struct inode *);
off_t inode_length (struct inode *);
bool inode_grow (struct inode *inode, off_t size, off_t offset);
block_sector_t inode_pos_to_real_sector (const struct inode *inode, off_t pos, bool allocate_new);
block_sector_t allocate_new_block (block_sector_t *table, block_sector_t idx);
block_sector_t allocate_new_index_inode (block_sector_t *table, block_sector_t idx);
off_t round_up_to_sector_boundary (off_t bytes);

#endif /* filesys/inode.h */
