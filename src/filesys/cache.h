#include "devices/block.h"
#include <list.h>

#define MAX_CACHE_SECTORS 64
#define BF_DAEMON_FLUSH_SLEEP_NS 100

struct buffer_cache_entry
{
	block_sector_t sector;              /* Sector number of disk location. */
	char data [BLOCK_SECTOR_SIZE];		/* Data contained in the cache */
	bool is_in_second_chance;			/* Whether the entry is in second chance */			
	bool is_dirty;						/* Whether the entry is in second dirty */			

	struct list_elem elem;              /* List element. */
};

void bc_init(void);
void bc_start_daemon (void);
void bc_block_read (block_sector_t sector, void *buffer, off_t offset, off_t size);
void bc_block_write (block_sector_t sector, void *buffer, off_t offset, off_t size);
void bc_remove (block_sector_t sector);
void bc_flush_all (void);

