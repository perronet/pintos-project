#ifndef FILESYS_INODE_H
#define FILESYS_INODE_H

#include <stdbool.h>
#include "filesys/off_t.h"
#include "devices/block.h"
#include "lib/kernel/list.h"

struct bitmap;

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44
#define DIRECT_BLOCKS 10
#define INDIRECT_BLOCKS 20
#define D_INDIRECT_BLOCKS 4
#define INDEX_MAIN_ENTRIES (DIRECT_BLOCKS + INDIRECT_BLOCKS + D_INDIRECT_BLOCKS)
#define INDEX_BLOCK_ENTRIES 64
#define UNUSED_SIZE (123-INDEX_MAIN_ENTRIES-INDEX_BLOCK_ENTRIES)


/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    block_sector_t start;               				/* First data sector. */
    block_sector_t parent;								/* The directory containing the inode */
    off_t length;                       				/* File size in bytes. */
    block_sector_t index[INDEX_MAIN_ENTRIES];			/* Main index of next blocks */
    block_sector_t index_block[INDEX_BLOCK_ENTRIES];	/* Supplementary index if it is an index block*/
    uint32_t is_index_block;							/* Is it a normal data block or an index block? */
    unsigned magic;                     			    /* Magic number. */
    uint32_t unused[UNUSED_SIZE];    					/* Not used. */
  };

/* In-memory inode. */
struct inode 
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    int open_fd_cnt;					/* Number of open fds on this dir. */
    int cwd_cnt;                        /* Number of processes that have this dir as cwd. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct inode_disk* data;            /* Inode content. */
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

#endif /* filesys/inode.h */
