#ifndef _SWAP_H
#define _SWAP_H 

void swap_init (void);

void swap_in (size_t slot, void* page);
size_t swap_out (const void *page);
void swap_free(size_t slot);

//assuming page size is mult of block size
#define SECTORS_PER_PAGE (PGSIZE / BLOCK_SECTOR_SIZE)

#endif
