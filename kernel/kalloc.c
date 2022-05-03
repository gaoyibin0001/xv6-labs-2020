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

struct run {
  struct run *next;
};

// struct {
//   struct spinlock lock;
//   struct run *freelist;
// } kmem;

struct fl_mem_lock{
  struct spinlock lock;
  struct run *freelist;
};

struct fl_mem_lock fl_kmem_locks[NCPU];

void freerange_with_lock(void *pa_start, void *pa_end, struct fl_mem_lock *kmem);
void kfree_with_lock(void *pa, struct fl_mem_lock *kmem);
void * kalloc_with_lock(struct fl_mem_lock *kmem);

int
getcpuid(void) {
  push_off();
  int id = cpuid();
  pop_off();
  return id;
}

char names_mem[NCPU][10];

void
kinit()
{ 
  int cpu_id;
  uint64 total_mem;
  uint64 start_mem = PGROUNDUP((uint64)end);
  total_mem = PHYSTOP - start_mem;
  uint64 each_share_mem = total_mem / NCPU;
  
  uint64 end_mem = start_mem + each_share_mem;
  for (cpu_id=0; cpu_id<NCPU; cpu_id++) {
    // char name[10];
    snprintf(names_mem[cpu_id], 10, "kmem-%d", cpu_id);
    initlock(&fl_kmem_locks[cpu_id].lock, names_mem[cpu_id]);
    freerange_with_lock((void*)start_mem, (void*)end_mem, &fl_kmem_locks[cpu_id]);
    start_mem = end_mem;
    end_mem += each_share_mem;
  }
  printf("4");
}

void
freerange_with_lock(void *pa_start, void *pa_end, struct fl_mem_lock *kmem)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree_with_lock(p, kmem);
}

void freerange(void *pa_start, void *pa_end)
{
  int cpu_id = getcpuid();
  freerange_with_lock(pa_start, pa_end, &fl_kmem_locks[cpu_id]);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree_with_lock(void *pa, struct fl_mem_lock *kmem)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem->lock);
  r->next = kmem->freelist;
  kmem->freelist = r;
  release(&kmem->lock);
}

void
kfree(void *pa)
{
  int cpu_id = getcpuid();
  kfree_with_lock(pa, &fl_kmem_locks[cpu_id]);
}

int
steal_mem_from_other_cpu(struct fl_mem_lock *kmem){
  struct fl_mem_lock *c_kmem;
  int length = 0;
  int steal_length;
  struct run *c_run, *steal_run_list;
  for (int cpu_id=0; cpu_id < NCPU; cpu_id++) {
    c_kmem = &fl_kmem_locks[cpu_id];
    if (c_kmem == kmem)
      continue;
    
    if (c_kmem->freelist) {
      acquire(&c_kmem->lock);
      acquire(&kmem->lock);
      c_run = c_kmem->freelist;
      while (c_run) {
        length++;
        c_run = c_run->next;
      }
      if (length == 1) {
        steal_length = 1;
      } else {
        steal_length = length/2;
      }
      if (steal_length <= 0) {
        printf("should panic!!!\n");
        continue;
      }
        
      c_run = c_kmem->freelist;
      for (int i=0; i<steal_length; i++) {
         c_run = c_run->next;
      }
      steal_run_list = c_kmem->freelist;
      c_kmem->freelist = c_run->next;
      // c_run->next = (void*) 0;
      // release(&c_kmem->lock);
      // todo 1. possible lock error
      // acquire(&kmem->lock);
      c_run->next = kmem->freelist;
      // steal_run_list->next = kmem->freelist;
      kmem->freelist = steal_run_list;
      release(&kmem->lock);
      release(&c_kmem->lock);
      // printf("fdfd");
      return 0;
    }
  }
  // printf("545");
  return -1;
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc_with_lock(struct fl_mem_lock *kmem)
{
  struct run *r;

  acquire(&kmem->lock);
  r = kmem->freelist;
  if(r)
    kmem->freelist = r->next;
  release(&kmem->lock);

  if (!r) {
    if (steal_mem_from_other_cpu(kmem) == 0){
        acquire(&kmem->lock);
        r = kmem->freelist;
        if(r)
          kmem->freelist = r->next;
        release(&kmem->lock);
    }
  }

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}

void *
kalloc(void)
{
 int cpu_id = getcpuid();
 void * r;
 r = kalloc_with_lock(&fl_kmem_locks[cpu_id]);
 return r;
}