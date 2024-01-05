#include "frame.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "filesys/off_t.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"

struct lock frame_clock_list_lock;
static struct list clock_list; // a cycle list for clock algorithm
static struct list_elem *clock_list_iterator;

static void
clock_list_next (void)
{
  clock_list_iterator
      = (list_next (clock_list_iterator) == list_tail (&clock_list))
            ? list_begin (&clock_list)
            : list_next (clock_list_iterator);
}

/**
 * @brief Set cur_frame to the frame to be evicted
 * @note This function gaurentees thread safety
 */
static void
clock_list_push_back (struct list_elem *elem)
{
  lock_acquire (&frame_clock_list_lock);
  if (list_empty (&clock_list))
    {
      clock_list_iterator = elem;
      list_push_back (&clock_list, elem);
    }
  else
    list_insert (clock_list_iterator, elem);
  lock_release (&frame_clock_list_lock);
}

/**
 * @brief Clock list remove
 * @param elem the elem in struct fte
 * @note This function gaurentees thread safety
 */
static void
clock_list_remove (struct list_elem *elem)
{
  ASSERT (elem != NULL);
  ASSERT (!list_empty (&clock_list));
  lock_acquire (&frame_clock_list_lock);
  if (clock_list_iterator == elem)
    clock_list_next ();
  list_remove (elem);
  lock_release (&frame_clock_list_lock);
}

/**
 * @brief Helper function for evicting a frame
 * @note This function gaurentees thread safety
 */
static struct fte *
clock_find_target_to_evict (void)
{
  lock_acquire (&frame_clock_list_lock);

  struct fte *evict_target;
  bool found = false;
  while (!found)
    {
      evict_target
          = list_entry (clock_list_iterator, struct fte, clock_list_elem);
      if (lock_try_acquire (&evict_target->lock))
        {
          if (pagedir_is_accessed (evict_target->owner->pagedir,
                                   evict_target->upage))
            /* if accessed, set accessed to false and move to next */
            {
              pagedir_set_accessed (evict_target->owner->pagedir,
                                    evict_target->upage, false);
              clock_list_next ();
            }
          else
            found = true;
          lock_release (&evict_target->lock);
        }
      else
        {
          clock_list_next ();
        }
    }

  // Now, cur_frame is the one that satisfies the condition:
  // 1. not accessed
  // 2. is a frame
  // so we can evict it
  ASSERT (evict_target != NULL);
  ASSERT (evict_target->type == SPTE_FRAME);
  ASSERT (evict_target->kpage != NULL);
  ASSERT (is_user_vaddr (evict_target->upage));

  clock_list_next ();
  list_remove (&evict_target->clock_list_elem);

  lock_release (&frame_clock_list_lock);
  return evict_target;
}

static unsigned
cur_frame_table_hash (const struct hash_elem *e, void *aux)
{
  const struct fte *fte = hash_entry (e, struct fte, cur_frame_table_elem);
  return hash_bytes (&fte->upage, sizeof fte->upage);
}

static bool
cur_frame_table_less (const struct hash_elem *a, const struct hash_elem *b,
                      void *aux)
{
  const struct fte *fte_a = hash_entry (a, struct fte, cur_frame_table_elem);
  const struct fte *fte_b = hash_entry (b, struct fte, cur_frame_table_elem);
  return fte_a->upage < fte_b->upage;
}

/**
 * @brief initialize current frame table of a thread
 * @param frame_table
 */
void
cur_frame_table_init (struct hash *frame_table)
{
  hash_init (frame_table, cur_frame_table_hash, cur_frame_table_less,
             frame_table);
}

/**
 * @brief find a frame in current frame table
 * @param upage user page
 */
struct fte *
cur_frame_table_find (void *upage)
{
  struct thread *t = thread_current ();
  struct hash *page_table = &t->frame_table;
  struct fte fte;
  fte.upage = upage;
  struct hash_elem *e = hash_find (page_table, &fte.cur_frame_table_elem);
  if (e != NULL)
    return hash_entry (e, struct fte, cur_frame_table_elem);
  return NULL;
}

void
frame_init (void)
{
  lock_init (&frame_clock_list_lock);
  list_init (&clock_list);
}

/**
 * @brief Automatically evict a frame from the memory
 */
void
frame_evict (void)
{
  fte_evict (clock_find_target_to_evict ());
}

/**
 * Create a frame and install it to current thread
 * @param upage user page
 * @param writable writable or not
 * @return fte if success, NULL if failed
 */
struct fte *
fte_create (void *upage, bool writable)
{
  // malloc fte
  struct fte *fte = malloc (sizeof (struct fte));
  if (fte == NULL)
    return NULL;

  fte->upage = upage;
  fte->writable = writable;
  fte->owner = thread_current ();
  fte->kpage = palloc_get_page_force (PAL_USER
                                      | PAL_ZERO); // eviction may happen here
  fte->mmap_entry = NULL;
  fte->type = SPTE_FRAME;

  if (install_page (fte->upage, fte->kpage, fte->writable) == false)
    {
      palloc_free_page (fte->kpage);
      free (fte);
      return NULL;
    }

  lock_init (&fte->lock);
  lock_acquire (&fte->lock);

  hash_insert (&fte->owner->frame_table, &fte->cur_frame_table_elem);

  ASSERT (cur_frame_table_find (upage));

  // initialize cur_frame at the first run
  clock_list_push_back (&fte->clock_list_elem);

  lock_release (&fte->lock);

  return fte;
}

/**
 * @brief attach a fte to file
 * @param file the file to attach
 * @param file_offset the offset of the file
 * @param upage the user page
 * @param writable writable or not
 * @return fte if success, NULL if failed
 */
struct fte *
fte_attach_to_file (struct file *file, uint32_t file_offset, void *upage,
                    bool writable)
{
  struct fte *fte = malloc (sizeof (struct fte));
  ASSERT (fte != NULL);
  ASSERT (file->mmap_entry != NULL);

  struct mmap_entry *mmap_entry = file->mmap_entry;

  // initialize fte
  fte->upage = upage;
  fte->writable = writable;
  fte->owner = thread_current ();
  fte->type = SPTE_FILE;
  fte->kpage = NULL;
  fte->file_offset = file_offset;
  fte->mmap_entry = mmap_entry;
  fte->size = file_length (file) - file_offset;
  if (fte->size > PGSIZE)
    fte->size = PGSIZE;
  lock_init (&fte->lock);

  // add fte to mmap_entry's fte_list
  list_push_back (&mmap_entry->fte_list, &fte->fte_elem);

  // add fte into table
  hash_insert (&fte->owner->frame_table, &fte->cur_frame_table_elem);

  return fte;
}

/**
 * @brief detach a fte from file
 * @param fte the frame table entry to detach
 */
void
fte_detach_from_file (struct fte *fte)
{
  ASSERT (has_acquired_filesys ());

  lock_acquire (&fte->lock);
  if (fte->kpage == NULL)
    {
      // allocate the frame
      fte->kpage = palloc_get_page_force (PAL_USER);
      // load data
      file_read_at (fte->mmap_entry->file, fte->kpage, fte->size,
                    fte->file_offset);
      // install page
      ASSERT (install_page (fte->upage, fte->kpage, fte->writable));
    }
  else
    {
      // write through if dirty
      if (pagedir_is_dirty (fte->owner->pagedir, fte->upage))
        {
          struct file *file = fte->mmap_entry->file;
          ASSERT (is_file (file));
          file_write_at (fte->mmap_entry->file, fte->upage, fte->size,
                         fte->file_offset);
        }
    }
  fte->mmap_entry = NULL;
  fte->type = SPTE_FRAME;
  clock_list_push_back (&fte->clock_list_elem);

  lock_release (&fte->lock);
}

/**
 * @brief evict a fte, copy from memory to swap
 * @param fte the frame table entry to evict
 */
void
fte_evict (struct fte *fte)
{
  ASSERT (is_user_vaddr (fte->upage));
  ASSERT (fte->kpage != NULL);
  ASSERT (fte->type != SPTE_SWAP);

  lock_acquire (&fte->lock);

  if (fte->type == SPTE_FILE)
    {
      // write back if dirty
      if (pagedir_is_dirty (fte->owner->pagedir, fte->upage))
        {
          acquire_filesys ();
          file_write_at (fte->mmap_entry->file, fte->upage, fte->size,
                         fte->file_offset);
          release_filesys ();
        }
    }

  // remove the virtual map from current thread
  pagedir_clear_page (fte->owner->pagedir, fte->upage);

  void *kpage = fte->kpage;
  fte->swap_id = swap_write (kpage);
  palloc_free_page (kpage);

  if (fte->type == SPTE_FRAME)
    fte->type = SPTE_SWAP;

  lock_release (&fte->lock);
}

/**
 * @brief unevict a fte, copy from swap to memory
 * @param fte the frame table entry to unevict
 */
void
fte_unevict (struct fte *fte)
{
  ASSERT (fte->owner == thread_current ());

  lock_acquire (&fte->lock);

  // force allocate memory, eviction may happen here
  void *new_kpage = palloc_get_page_force (PAL_USER);

  if (fte->type == SPTE_SWAP)
    {
      swap_id_t swap_id = fte->swap_id; // from which to restore

      // restore memory
      swap_read (swap_id, new_kpage);
      swap_free (swap_id);
    }
  else if (fte->type == SPTE_FILE)
    {
      // write back if dirty
      if (pagedir_is_dirty (fte->owner->pagedir, fte->upage))
        {
          acquire_filesys ();
          file_write_at (fte->mmap_entry->file, fte->upage, fte->size,
                         fte->file_offset);
          release_filesys ();
        }
      // load data
      acquire_filesys ();
      file_read_at (fte->mmap_entry->file, new_kpage, fte->size,
                    fte->file_offset);
      release_filesys ();
    }
  else
    {
      PANIC ("should not reach here");
    }

  // install virtual map
  fte->kpage = new_kpage;
  ASSERT (install_page (fte->upage, fte->kpage, fte->writable));

  if (fte->type == SPTE_SWAP)
    {
      fte->type = SPTE_FRAME;
      clock_list_push_back (&fte->clock_list_elem);
    }

  lock_release (&fte->lock);
}

/**
 * @brief destroy a fte
 * @param fte to destroy
 * @note do not free the physical page since it will
 * be freed in process_exit by pagedir_destroy (pd);
 * @note this may write to through file if dirty
 * so the file should not be closed
 */
void
fte_destroy (struct fte *fte)
{
  ASSERT (fte->owner == thread_current ());

  // remove from thread's frame table
  /// TODO : check if it is thread safe
  hash_delete (&fte->owner->frame_table, &fte->cur_frame_table_elem);

  // remove from clock list
  if (fte->type == SPTE_SWAP)
    swap_free (fte->swap_id);
  else if (fte->type == SPTE_FILE)
    {
      // write back if dirty
      if (pagedir_is_dirty (fte->owner->pagedir, fte->upage))
        {
          struct file *file = fte->mmap_entry->file;
          ASSERT (is_file (file));
          bool has_acquired = has_acquired_filesys ();
          if (!has_acquired)
            acquire_filesys ();
          file_write_at (file, fte->upage, fte->size, fte->file_offset);
          if (!has_acquired)
            release_filesys ();
        }
    }
  else if (fte->type == SPTE_FRAME)
    {
      clock_list_remove (&fte->clock_list_elem);
    }

  free (fte);
}

/**
 * @brief destroy a mmap_entry
 * @param mmap_entry to destroy
 * @param destroy_func function to destroy fte
 */
void
mmap_destroy (struct mmap_entry *mmap_entry,
              mmap_elem_destroy_func destroy_func)
{
  ASSERT (mmap_entry != NULL);
  ASSERT (destroy_func != NULL);
  for (struct list_elem *e = list_begin (&mmap_entry->fte_list);
       e != list_end (&mmap_entry->fte_list);)
    {
      struct fte *fte = list_entry (e, struct fte, fte_elem);
      e = list_remove (e);
      ASSERT (fte->mmap_entry == mmap_entry);
      destroy_func (fte);
    }

  mmap_entry->file->mmap_entry = NULL;
  free (mmap_entry);
}
