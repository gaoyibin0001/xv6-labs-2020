// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.
// #define num_free_list ((PGROUNDDOWN(PHYSTOP) - PGROUNDUP((uint64)end)) / PGSIZE)
// (PHYSTOP - KERNBASE) / (1024 * 4) = 32 * 1024


struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

struct {
  struct spinlock lock;
  int free_list_refcount[32 * 1024];
} fl_fefcount;


void
fl_fefcount_minus(uint64 r)
{
  int free_list_index, refcount;
  free_list_index = (r -KERNBASE) >> PGSHIFT;
  acquire(&fl_fefcount.lock);
  refcount = fl_fefcount.free_list_refcount[free_list_index];
  if (refcount <= 1)
    kfree((char*)r);
  else
    fl_fefcount.free_list_refcount[free_list_index] -= 1;
  release(&fl_fefcount.lock);
}

void
fl_fefcount_add(uint64 r)
{
  int free_list_index;
  free_list_index = (r -KERNBASE) >> PGSHIFT;
  acquire(&fl_fefcount.lock);
  fl_fefcount.free_list_refcount[free_list_index] += 1;
  release(&fl_fefcount.lock);
}

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");
  
  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;
  int free_list_index;
  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r){
    free_list_index = (((uint64) r) -KERNBASE) >> PGSHIFT;
    fl_fefcount.free_list_refcount[free_list_index] = 1;
    kmem.freelist = r->next;
  }
    
  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
