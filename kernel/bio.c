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

char names_bucket[NBUCKET][20];


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
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    initsleeplock(&b->lock, "buffer");
  }

  struct bucket *bt;
  int bucket_id;
  for(bucket_id = 0; bucket_id < NBUCKET; bucket_id++){
    bt = &bcache.hash_buckets[bucket_id];
    snprintf(names_bucket[bucket_id], 20, "bcache-%d", bucket_id);
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
  struct bucket *bucket_hit = &bcache.hash_buckets[bucket_id];
  acquire(&bucket_hit->lock);

  // Is the block already cached?
  for(b = bucket_hit->head; b; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      b->ticks = sys_uptime_bucket();
      release(&bucket_hit->lock);
      acquiresleep(&b->lock);

      return b;
    }
  }
  release(&bucket_hit->lock);

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  struct buf *lru_buf = (void*)0; 
  struct buf *c_buf;
  acquire(&bcache.lock);
  acquire(&bucket_hit->lock);
  // checkout again !!!
  for(b = bucket_hit->head; b; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      b->ticks = sys_uptime_bucket();
      release(&bucket_hit->lock);
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // allock new buf to bucket
  int current_buf_bucket_id = -2, previous_buf_bucket_id= -1;
  struct bucket *current_buf_bucket = (void*)0;
  struct bucket *previous_buf_bucket = (void*)0;
  for(c_buf = bcache.buf; c_buf < bcache.buf+NBUF; c_buf++){
    current_buf_bucket_id = get_bucket_id(c_buf->dev, c_buf->blockno);
    current_buf_bucket = &bcache.hash_buckets[current_buf_bucket_id];
    if (lru_buf !=0) {
        previous_buf_bucket_id = get_bucket_id(lru_buf->dev, lru_buf->blockno);
        previous_buf_bucket = &bcache.hash_buckets[previous_buf_bucket_id];
    }
    if (c_buf->ticks != 0 && bucket_id != current_buf_bucket_id && current_buf_bucket_id != previous_buf_bucket_id) {
       acquire(&current_buf_bucket->lock);
    }
    if(c_buf->refcnt == 0) {
      if (c_buf->ticks == 0){
        if (lru_buf !=0) {
          if (lru_buf->ticks != 0 && bucket_id != previous_buf_bucket_id) {
            release(&previous_buf_bucket->lock);
          }
        }
        
        lru_buf = c_buf;
        break;
      }
      else if (lru_buf==0){
        lru_buf = c_buf;
        // not release current lock
      }
      else if (c_buf->ticks < lru_buf->ticks){
        
        // hold current lock
        // release previous lock
       if (lru_buf->ticks != 0 && bucket_id != previous_buf_bucket_id && current_buf_bucket_id != previous_buf_bucket_id) {
          release(&previous_buf_bucket->lock);
        }

       lru_buf = c_buf;
      }
      else {
        if (c_buf->ticks != 0 && bucket_id != current_buf_bucket_id && current_buf_bucket_id != previous_buf_bucket_id) {
          release(&current_buf_bucket->lock);
        }
      }
    }
    else {
      if (c_buf->ticks != 0 && bucket_id != current_buf_bucket_id && current_buf_bucket_id != previous_buf_bucket_id) {
        release(&current_buf_bucket->lock);
         }
       
    }
  } 
  
  

  if (lru_buf != 0){
    struct bucket *old_bucket = (void*) 0;
    int old_bucket_id = get_bucket_id(lru_buf->dev, lru_buf->blockno);
    old_bucket = &bcache.hash_buckets[old_bucket_id];
    if (lru_buf->ticks != 0 && old_bucket_id != bucket_id) {  // so lru belong to bucket other than current bucket id
        struct buf * check_buf;
        
        if (lru_buf == old_bucket->head) {
          old_bucket->head = lru_buf->next;
        }
        else {
          for (check_buf=old_bucket->head; check_buf->next; check_buf=check_buf->next){
            if (check_buf->next == lru_buf) {
                check_buf->next = lru_buf->next;
                break;
            }
          }
        }
      }

     lru_buf->refcnt = 1;
     if (lru_buf->ticks == 0 || (lru_buf->ticks != 0 && old_bucket_id != bucket_id)) { // if new buf not belong to any bucket or not same bucket
        lru_buf->next = bucket_hit->head;
        bucket_hit->head = lru_buf;
     }
     release(&bcache.lock);
     if (lru_buf->ticks != 0 && old_bucket_id != bucket_id) {
        release(&old_bucket->lock);
      }
     
      lru_buf->dev = dev;
      lru_buf->blockno = blockno;
      lru_buf->valid = 0;
      
      lru_buf->ticks = sys_uptime_bucket();
      
      release(&bucket_hit->lock);
      acquiresleep(&lru_buf->lock);
      return lru_buf;
    }
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
  struct bucket *bucket_hit = &bcache.hash_buckets[bucket_id];
  acquire(&bucket_hit->lock);
  b->refcnt--;
  if (b->refcnt == 0) {
    b->ticks = sys_uptime_bucket();
  }
  release(&bucket_hit->lock);
}

void
bpin(struct buf *b) {
  int bucket_id = get_bucket_id(b->dev, b->blockno);
  struct bucket *bucket_hit = &bcache.hash_buckets[bucket_id];
  acquire(&bucket_hit->lock);
  b->refcnt++;
  release(&bucket_hit->lock);
}

void
bunpin(struct buf *b) {
  int bucket_id = get_bucket_id(b->dev, b->blockno);
  struct bucket *bucket_hit = &bcache.hash_buckets[bucket_id];
  acquire(&bucket_hit->lock);
  b->refcnt--;
  release(&bucket_hit->lock);
}


