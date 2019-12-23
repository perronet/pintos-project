struct block  *swap_device;

static struct bitmap *swap_bm;

void 
swap_init ()
{
	swap_device = block_get_role (BLOCK_SWAP);
	ASSERT (swap_device != NULL);
	size_t bmsize = block_size(swap_device) / SECTORS_PER_PAGE;
	swap_bm = bitmap_create(bmsize);
	ASSERT (swap_bm != NULL);

	bitmap_set_all(swap_bm, true);
}	

void 
swap_in (size_t slot, void* page)
{
	size_t swap_addr_base = slot * SECTORS_PER_PAGE;
	for (size_t i = 0; i < SECTORS_PER_PAGE; i++)
		{
			size_t from_addr = swap_addr_base + i;
			size_t to_addr = page + i * BLOCK_SECTOR_SIZE;

			block_read (swap_device, from_addr, to);
		}

	swap_free (slot);
}

size_t 
swap_out (const void* page)
{
	size_t slot = bitmap_scan_and_flip (swap_bm, 0, 1, true);

	if (slot != BITMAP_ERROR)
		{
			size_t swap_addr_base = slot * SECTORS_PER_PAGE;
			for (size_t i = 0; i < SECTORS_PER_PAGE; i++)
				{
					size_t from_addr = page + i * BLOCK_SECTOR_SIZE;
					size_t to_addr = swap_addr_base + i;
					block_write (swap_device, to_addr, from_addr);
				}	
		}
	else
		return NULL;
}

void swap_free(size_t *slot)
{
	bitmap_flip (swap_bm, slot);
}
