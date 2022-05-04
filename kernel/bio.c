// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"


struct bucket {
  struct buf *head;
  struct spinlock lock;
};


struct {
  struct spinlock lock;
  struct buf buf[NBUF];
  struct bucket hash_buckets[NBUCKET];
  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  // struct buf head;
} bcache;

char names_bucket[NCPU][10];


uint64
sys_uptime_bucket(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

void
binit(void)
{
  struct buf *b;

  initlock(&bcache.lock, "bcache");

  // Create linked list of buffers
  // bcache.head.prev = &bcache.head;
  // bcache.head.next = &bcache.head;
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    // b->next = bcache.head.next;
    // b->prev = &bcache.head;
    initsleeplock(&b->lock, "buffer");
    // bcache.head.next->prev = b;
    // bcache.head.next = b;
  }

  struct bucket *bt;
  int bucket_id;
  for(bt = bcache.hash_buckets; bt < bcache.hash_buckets+NBUCKET; bt++){
    // b->next = bcache.head.next;
    // b->prev = &bcache.head;
    bucket_id = bt - bcache.hash_buckets;
    snprintf(names_bucket[bucket_id], 10, "bcache-%d", bucket_id);
    initlock(&bt->lock, names_bucket[bucket_id]);
  }
}

int 
get_bucket_id(uint dev, uint blockno)
{
  int bucket_id;
  bucket_id = (dev + blockno) % NBUCKET;
  return bucket_id;
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  int bucket_id = get_bucket_id(dev, blockno);

  // acquire(&bcache.lock);
  struct bucket bucket_hit = bcache.hash_buckets[bucket_id];
  acquire(&bucket_hit.lock);

  // Is the block already cached?
  for(b = bucket_hit.head; b; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bucket_hit.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  release(&bucket_hit.lock);

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  acquire(&bcache.lock);
  

  struct buf *lru_buf = (void*)0; 
  for(b = bcache.buf; b <= bcache.buf+NBUF; b++){
    if(b->refcnt == 0) {
      if (b->ticks == 0){
        lru_buf = b;
        break;
      }
      else if (lru_buf->ticks==0){
        lru_buf = b;
      }
      else if (b->ticks < lru_buf->ticks){
        lru_buf = b;
      }
    }
  } 

  if (lru_buf != 0){
      lru_buf->dev = dev;
      lru_buf->blockno = blockno;
      lru_buf->valid = 0;
      lru_buf->refcnt = 1;
      lru_buf->ticks = sys_uptime_bucket();

      acquire(&bucket_hit.lock);
      lru_buf->next = bucket_hit.head;
      bucket_hit.head = lru_buf;
      release(&bucket_hit.lock);

      release(&bcache.lock);
      // release(&bucket_hit.lock);
      acquiresleep(&b->lock);
      return b;
    }
  release(&bcache.lock);
  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");
  releasesleep(&b->lock);
  // sys_uptime_bucket
  int bucket_id = get_bucket_id(b->dev, b->blockno);
  // acquire(&bcache.lock);
  struct bucket bucket_hit = bcache.hash_buckets[bucket_id];
  acquire(&bucket_hit.lock);
  b->refcnt--;
  struct buf * check_buf;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    // b->next->prev = b->prev;
    // b->prev->next = b->next;
    // b->next = bcache.head.next;
    // b->prev = &bcache.head;
    // bcache.head.next->prev = b;
    // bcache.head.next = b;
    b->ticks = sys_uptime_bucket();
    if (b == bucket_hit.head) bucket_hit.head = bucket_hit.head->next;
    for (check_buf=bucket_hit.head; check_buf->next; check_buf=check_buf->next){
       if (check_buf->next == b) {
          check_buf->next = check_buf->next->next;
       }
    }
  }
  
  release(&bucket_hit.lock);
}

void
bpin(struct buf *b) {
  acquire(&bcache.lock);
  b->refcnt++;
  release(&bcache.lock);
}

void
bunpin(struct buf *b) {
  acquire(&bcache.lock);
  b->refcnt--;
  release(&bcache.lock);
}


