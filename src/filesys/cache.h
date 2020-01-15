#include "devices/block.h"
#include "threads/synch.h"
#include <list.h>

#define MAX_CACHE_SECTORS 64
#define BC_DAEMON_FLUSH_SLEEP_MS 1000
#define MAX_READ_AHEAD 10
#define EMPTY_SECTOR SIZE_MAX

struct buffer_cache_entry
{
	block_sector_t sector;              /* Sector number of disk location. */
	char data [BLOCK_SECTOR_SIZE];		/* Data contained in the cache */
	bool is_in_second_chance;			/* Whether the entry is in second chance */			
	bool is_dirty;						/* Whether the entry is in second dirty */	
	unsigned int readers;
	struct lock elock;				/* Used to handle asynchronous reads */
};

void bc_init(void);
void bc_start_daemon (void);
void bc_block_read (block_sector_t sector, void *buffer, off_t offset, off_t size);
void bc_request_read_ahead (block_sector_t sector);
void bc_block_write (block_sector_t sector, void *buffer, off_t offset, off_t size);
void bc_remove (block_sector_t sector);
void bc_flush_all (void);

