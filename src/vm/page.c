#include "vm/page.h"
#include "userprog/process.h"

#define STACK_MAX (1 << 23) //256kb

static bool install_page (void *upage, void *kpage, bool writable);
 
static unsigned page_hash_func (const struct hash_elem *e, void *aux UNUSED)
{
  struct s_pte *spte = hash_entry(e, struct s_pte, elem);
  return hash_int((int) spte->vaddr);
}

static bool page_less_func (const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED)
{
  struct s_pte *spte_a = hash_entry(a, struct s_pte, elem);
  struct s_pte *spte_b = hash_entry(b, struct s_pte, elem);
  if (spte_a->vaddr < spte_b->vaddr)
      return true;

  return false;
}


void s_page_table_init(struct hash * s_page_table){
    hash_init (s_page_table, page_hash_func, page_less_func, NULL);

}

struct s_pte* s_pte_alloc(struct cur_file_info * cur_file_info, void * vaddr){
    struct s_pte * spte = (struct s_pte *) malloc(sizeof(struct s_pte));
    spte->cur_file_info =  cur_file_info;
    spte -> vaddr = vaddr;
    spte -> is_loaded = false;
    hash_insert(&thread_current()->s_page_table, &spte->elem);

    return spte;
}

static void s_page_action_func (struct hash_elem *e, void *aux UNUSED)
{
    //printf("page free called \n");
  struct s_pte *spte = hash_entry(e, struct s_pte, elem);
    //if(is_loaded(spte->vaddr)){
    fte_free(fte_search_by_spte(spte)->frame);
    //  fte_free(pagedir_get_page(thread_current()->pagedir, spte->vaddr));
     // pagedir_clear_page(thread_current()->pagedir, spte->vaddr);
    //}
    free(spte);
}

void s_page_table_destroy(){
    hash_destroy (&thread_current()->s_page_table, s_page_action_func);
}


struct s_pte* s_pte_search_by_vaddr(void* vaddr){
    struct s_pte spte;
    spte.vaddr = pg_round_down(vaddr);
    struct hash_elem *e = hash_find(&thread_current()->s_page_table, &spte.elem);
    

    if (e == NULL)
        return NULL;

    return hash_entry (e, struct s_pte, elem);
}

bool load_page(void * vaddr){

    struct s_pte * spte = s_pte_search_by_vaddr(vaddr);
    enum palloc_flags flags = PAL_USER;
    if(spte == NULL){
        return false;
    }
    if(spte -> is_loaded)
        return true;

    if (spte->cur_file_info-> page_read_bytes == 0){
        flags == PAL_USER & PAL_ZERO;
    }

    void * frame = palloc_get_page(flags);
    
    
    if(frame == NULL){
        frame_evict();
	frame =  palloc_get_page(flags);
    }

    fte_search_by_frame(frame)->spte = spte;

    /* Load this page. */
    lock_acquire(&file_lock);
    if (file_read (spte->cur_file_info->file, frame, spte->cur_file_info -> page_read_bytes) != (int) spte->cur_file_info -> page_read_bytes)
    {
        palloc_free_page(frame);
        lock_release(&file_lock);
        return false; 
    }

    lock_release(&file_lock);

    memset (frame + spte->cur_file_info->page_read_bytes, 0, spte->cur_file_info-> page_zero_bytes);

    /* Add the page to the process's address space. */
    if (!install_page (spte-> vaddr, frame, spte->cur_file_info->writable)) 
    {
        palloc_free_page(frame);
        return false; 
    }
    spte-> is_loaded = true;

    return true;
}

// bool is_loaded(void * page){
//    struct s_pte * spte = s_pte_search_by_vaddr(page); 
//     if(fte_search_by_spte(spte) == NULL)
//         return false;
    
//     return true;
// }

static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}

bool stack_growth(void* vaddr)
{
	//check stack size
  size_t new_size = PHYS_BASE - pg_round_down(vaddr);
  if(new_size > (1 << 23)) return false;

  struct cur_file_info* cfi = malloc(sizeof(struct cur_file_info));
  struct s_pte* spte = s_pte_alloc(cfi, pg_round_down(vaddr));
  spte->is_loaded = true;
  spte->cur_file_info->writable = true;
  
  uint8_t* frame = palloc_get_page(PAL_USER);
  if(frame == NULL)
  {
    free(cfi);
    free(spte);
    return false;    
  }
  fte_search_by_frame(frame)->spte = spte;
  if(!install_page(spte->vaddr, frame, spte->cur_file_info->writable))
  {
    free(spte->cur_file_info);
    free(spte);
    palloc_free_page(frame);
    return false;
  }

  return true;
}
