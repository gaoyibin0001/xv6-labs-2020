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
  // printf("4");
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
  int length;
  int steal_length;
  struct run *c_run, *steal_run_list;
  // printf("current alloc cpu%d\n", kmem-fl_kmem_locks);
  for (int cpu_id=0; cpu_id < NCPU; cpu_id++) {
    c_kmem = &fl_kmem_locks[cpu_id];
    length = 0;
    if (c_kmem == kmem)
      continue;
    
    if (c_kmem < kmem) {
      // printf("lock1111: %d\n", c_kmem-fl_kmem_locks);
       acquire(&c_kmem->lock);
      //  printf("lock2222: lockid: %d\n", kmem-fl_kmem_locks);
       acquire(&kmem->lock);
    } else {
      // printf("lock3333: lockid: %d\n", kmem-fl_kmem_locks);
      acquire(&kmem->lock);
      // printf("lock4444 lockid: %d\n", c_kmem-fl_kmem_locks);
      acquire(&c_kmem->lock);
    }
    
    if (c_kmem->freelist) {
      // todo deadlock. resolve by define order of lock
      c_run = c_kmem->freelist;
      while (c_run) {
        length++;
        c_run = c_run->next;
      }

      // if (length <= 10) {
      //   printf("should panic!!!\n");
      //   release(&kmem->lock);
      //   release(&c_kmem->lock);
      //   continue;
      // }

      // printf("before cut length: %d, cpuid: %d\n", length, cpu_id);
      if (length <= 100) {
        steal_length = length;
      } else {
        steal_length = length/2;
      }
      
        
      c_run = c_kmem->freelist;
      for (int i=0; i<(length-steal_length) && c_run->next; i++) {
         c_run = c_run->next;
      }
      // steal_run_list = c_kmem->freelist;
      // c_kmem->freelist = c_run->next;
      if (length == steal_length) {
        steal_run_list = c_kmem->freelist;
        c_kmem->freelist = (void*) 0;
      } else {
        steal_run_list = c_run->next;
        c_run->next = (void*) 0;
      }
     
      // steal_run_list = c_run;
      // c_run = (void*) 0;

      length = 0;
      c_run = c_kmem->freelist;
      while (c_run) {
        length++;
        c_run = c_run->next;
      }
      // printf("after cut length: %d\n", length);
      // release(&c_kmem->lock);
      // todo 1. possible lock error
      // acquire(&kmem->lock);
      // c_run->next = kmem->freelist;
      // steal_run_list->next = kmem->freelist;
      
      // c_run->next = kmem->freelist
      c_run = kmem->freelist;
      kmem->freelist = steal_run_list;
      // c_run = kmem->freelist;
      while(steal_run_list->next) {
        steal_run_list = steal_run_list->next;
      }
      steal_run_list->next = c_run; 
      
      // kmem->freelist = c_run->next;

      if (c_kmem < kmem) {
        // printf("lock5555\n");
       release(&kmem->lock);
      //  printf("lock6666\n");
      release(&c_kmem->lock);
    } else {
      // printf("lock7777\n");
      release(&c_kmem->lock);
      // printf("lock8888\n");
      release(&kmem->lock);
      
    }
      
      // printf("fdfd\n");
      return 0;
    } else {
      if (c_kmem < kmem) {
        // printf("lock5555\n");
       release(&kmem->lock);
      //  printf("lock6666\n");
      release(&c_kmem->lock);
    } else {
      // printf("lock7777\n");
      release(&c_kmem->lock);
      // printf("lock8888\n");
      release(&kmem->lock);
      
    }

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

  // acquire(&kmem->lock);
  if (!r) {
    
    if (steal_mem_from_other_cpu(kmem) == 0){
        acquire(&kmem->lock);
        r = kmem->freelist;
        if(r)
          kmem->freelist = r->next;
        release(&kmem->lock);
    }
    
  }
  // release(&kmem->lock);

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