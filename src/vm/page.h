#ifndef _PAGE_H
#define _PAGE_H 

#include "filesys/off_t.h"
#include "threads/interrupt.h"


#define NORMAL    0b00000
#define MMF       0b00100
#define LAZY      0b01000

#define UNLOADED  0b00000
#define PRESENT   0b00001
#define SWAPPED   0b00010

#define IS_NORMAL(page) (page & NORMAL)
#define IS_MMF(page)    (page & MMF)
#define IS_LAZY(page)   (page & LAZY)

#define IS_UNLOADED(page)  (page & UNLOADED)
#define IS_PRESENT(page)   (page & PRESENT)
#define IS_SWAPPED(page)   (page & SWAPPED)

enum pt_status
  {
    //Normal page
    PAGE_PRESENT  = NORMAL  | PRESENT,
    PAGE_SWAPPED  = NORMAL  | SWAPPED,

    //Memory mapped file page
    MMF_UNLOADED  = MMF     | UNLOADED,
    MMF_PRESENT   = MMF     | PRESENT,
    MMF_SWAPPED   = MMF     | SWAPPED,

    //Lazy loading page
    LAZY_UNLOADED = LAZY    | UNLOADED,
    LAZY_PRESENT  = LAZY    | PRESENT,
    LAZY_SWAPPED  = LAZY    | SWAPPED,
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
    size_t swap_slot;
    struct pt_suppl_mmf *mmf;

  	struct hash_elem elem;
  };


void pt_suppl_init (struct hash *table);
bool pt_suppl_handle_page_fault (void * vaddr, struct intr_frame *f);
struct pt_suppl_entry * pt_suppl_get (struct hash *table, void *page);
bool pt_suppl_add (struct hash *table, struct pt_suppl_entry *entry);
void pt_suppl_remove (struct hash *table, struct pt_suppl_entry *entry);
bool pt_suppl_add_mmf (struct file *file, off_t offset, 
                       uint8_t *page_addr, uint32_t read_bytes);
void pt_suppl_flush_mmf (struct pt_suppl_entry *entry);
bool pt_suppl_page_in (struct pt_suppl_entry *entry);
void pt_suppl_page_out (struct hash *table, void *page);
void pt_suppl_free (struct hash *table);

void pt_suppl_grow_stack (void *top);

unsigned pt_suppl_hash (const struct hash_elem *he, void *aux UNUSED);
bool pt_suppl_less (const struct hash_elem *ha, const struct hash_elem *hb,
                    void *aux UNUSED);
#endif
