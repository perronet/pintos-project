#ifndef _PAGE_H
#define _PAGE_H 

#include "filesys/off_t.h"
#include "threads/interrupt.h"

#define NORMAL    0b0000
#define MMF       0b0100
#define LAZY      0b1000

#define UNLOADED  0b0000
#define PRESENT   0b0001
#define SWAPPED   0b0010

/* Starting from the right:
  -2 bits are for the presence
  -2 bits are for the type of page
*/
#define PRESENCE_MASK 0b0011
#define TYPE_MASK     0b1100

//Type
#define SET_TYPE(status, new_type) \
{ status = (status & PRESENCE_MASK) | new_type; }

#define IS_NORMAL(status) ((status & TYPE_MASK) == NORMAL)
#define IS_MMF(status)    ((status & TYPE_MASK) == MMF)
#define IS_LAZY(status)   ((status & TYPE_MASK) == LAZY)

//Presence 
#define SET_PRESENCE(status, new_presence) \
{ status = (status & TYPE_MASK) | new_presence; }

#define IS_UNLOADED(status)  ((status & PRESENCE_MASK) == UNLOADED)
#define IS_PRESENT(status)   ((status & PRESENCE_MASK) == PRESENT)
#define IS_SWAPPED(status)   ((status & PRESENCE_MASK) == SWAPPED)

enum pt_status
  {
    //Normal status
    status_PRESENT  = NORMAL  | PRESENT,
    status_SWAPPED  = NORMAL  | SWAPPED,

    //Memory mapped file status
    MMF_UNLOADED  = MMF     | UNLOADED,
    MMF_PRESENT   = MMF     | PRESENT,
    MMF_SWAPPED   = MMF     | SWAPPED, //TODO MAYBE DELETE AND FLUSH, SETTING ULOADED?

    //Lazy loading page
    LAZY_UNLOADED = LAZY    | UNLOADED,
    LAZY_PRESENT  = LAZY    | PRESENT,
    LAZY_SWAPPED  = LAZY    | SWAPPED,
  };

/* Used both for lazy loading and mmf */
struct pt_suppl_file_info
  {
  	struct file *file;
    int map_id;
    off_t offset;
    uint32_t read_bytes;

    uint32_t zero_bytes;
    bool writable;
  };

struct pt_suppl_entry
  {
  	void *vaddr;
  	enum pt_status status;
    size_t swap_slot;
    struct pt_suppl_file_info *file_info;

  	struct hash_elem elem;
  };

void pt_suppl_init (struct hash *table);
struct pt_suppl_entry * pt_suppl_get_entry_by_addr(const void *vaddr);
bool pt_suppl_handle_page_fault (void * vaddr, struct intr_frame *f);
int pt_suppl_handle_mmap (struct file *f, void *start_page);
void pt_suppl_handle_unmap (int map_id);
struct pt_suppl_entry * pt_suppl_get (struct hash *table, void *page);
bool pt_suppl_add (struct hash *table, struct pt_suppl_entry *entry);
void pt_suppl_destroy (struct pt_suppl_entry *entry);
bool pt_suppl_add_mmf (struct file *file, off_t offset, 
                       uint8_t *page_addr, uint32_t read_bytes);
bool pt_suppl_add_lazy (struct file *file, off_t offset, uint8_t *page_addr, 
                uint32_t read_bytes, uint32_t zero_bytes, bool writable);
void pt_suppl_flush_mmf (struct pt_suppl_entry *entry);
bool pt_suppl_page_in (struct pt_suppl_entry *entry);
void pt_suppl_page_out (struct hash *table, void *page);
void pt_suppl_free (struct hash *table);

void pt_suppl_grow_stack (void *top);

unsigned pt_suppl_hash (const struct hash_elem *he, void *aux UNUSED);
bool pt_suppl_less (const struct hash_elem *ha, const struct hash_elem *hb,
                    void *aux UNUSED);
#endif
