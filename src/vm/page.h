#ifndef _PAGE_H
#define _PAGE_H 

#endif

enum pt_status
  {
    PRESENT,
    SWAPPED,
    MMF_UNLOADED,
    MMF_PRESENT,
    MMF_SWAPPED
  };

struct pt_suppl_mmf
  {
  	struct file *file;
    off_t offset;
    uint32_t read_bytes;
  };

struct pt_suppl_entry
  {
  	void *vaddr;
  	enum pt_status status;
    struct pt_suppl_mmf *mmf;

  	struct hash_elem elem;
  };


struct pt_suppl_entry * pt_suppl_get (struct hash *table, void *vaddr);
bool pt_suppl_add (struct hash *table, struct pt_suppl_entry *entry);
void pt_suppl_remove (struct hash *table, struct pt_suppl_entry *entry);
bool pt_suppl_add_mmf (struct file *file, off_t offset, 
                       uint8_t *page_addr, uint32_t read_bytes);
void pt_suppl_flush_mmf (struct pt_suppl_entry *entry);
bool page_in (struct pt_suppl_entry *entry);
void pt_suppl_free (struct hash *table);

void pt_suppl_grow_stack (void *top);

unsigned pt_suppl_hash (const struct hash_elem *he, void *aux UNUSED);
bool pt_suppl_less (const struct hash_elem *ha, const struct hash_elem *hb,
                    void *aux UNUSED);
