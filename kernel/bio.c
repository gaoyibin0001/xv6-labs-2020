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
  //printf("hit uptime 1\n");
  acquire(&tickslock);
  //printf("hit uptime 2\n");
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
  // ////printf("aa: %d\n",bcache.lock.locked);
  for(bucket_id = 0; bucket_id < NBUCKET; bucket_id++){
    // b->next = bcache.head.next;
    // b->prev = &bcache.head;
    // bucket_id = bt - bcache.hash_buckets;
    bt = &bcache.hash_buckets[bucket_id];
    snprintf(names_bucket[bucket_id], 20, "bcache-%d", bucket_id);
    initlock(&bt->lock, names_bucket[bucket_id]);
    // ////printf("aa: %d\n",bcache.lock.locked);
  }
  // ////printf("545454");
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
  //printf("hit bget\n");
  struct buf *b;
  int bucket_id = get_bucket_id(dev, blockno);
  printf("dev: %d, blockno: %d\n", dev, blockno);
  struct bucket *bucket_hit = &bcache.hash_buckets[bucket_id];
  //printf("hit bget 1\n");
  acquire(&bucket_hit->lock);
  //printf("hit bget 2\n");

  // Is the block already cached?
  for(b = bucket_hit->head; b; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      b->ticks = sys_uptime_bucket();
      release(&bucket_hit->lock);
      acquiresleep(&b->lock);
      printf("hit bucket id: %d\n", bucket_id);
      return b;
    }
  }
  release(&bucket_hit->lock);

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  // printf("hit bget 3\n");
  
  // printf("hit bget 4\n");
  printf("bucket_id:%d\n", bucket_id);

  struct buf *lru_buf = (void*)0; 
  acquire(&bcache.lock);
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    if(b->refcnt == 0) {
      if (b->ticks == 0){
        lru_buf = b;
        break;
      }
      else if (lru_buf==0 || b->ticks < lru_buf->ticks){
        lru_buf = b;
      }
      // lru_buf->ticks==0
      // else if (b->ticks < lru_buf->ticks){
      //   lru_buf = b;
      // }
    }
  } 
  
  

  if (lru_buf != 0){
    struct bucket *old_bucket = (void*) 0;
    int old_bucket_id = get_bucket_id(lru_buf->dev, lru_buf->blockno);
    if (lru_buf->ticks != 0 && old_bucket_id != bucket_id) {  // so lru belong to bucket other than current bucket id
        old_bucket = &bcache.hash_buckets[old_bucket_id];
        struct buf * check_buf;
        if (old_bucket_id < bucket_id) {
          acquire(&old_bucket->lock);
          acquire(&bucket_hit->lock);
        }
        else {
          acquire(&bucket_hit->lock);
          acquire(&old_bucket->lock);
        }
        
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
      else {
        acquire(&bucket_hit->lock);
      }

     lru_buf->refcnt = 1;
    // printf("bucket head: %p\n", bucket_hit->head);
    // printf("old bucket id %d, new bucket_id %d\n", old_bucket_id, bucket_id);
     if (lru_buf->ticks == 0 || (lru_buf->ticks != 0 && old_bucket_id != bucket_id)) { // if new buf not belong to any bucket or not same bucket
        lru_buf->next = bucket_hit->head;
        bucket_hit->head = lru_buf;
     }
     
     release(&bcache.lock);
     
      lru_buf->dev = dev;
      lru_buf->blockno = blockno;
      lru_buf->valid = 0;
      
      lru_buf->ticks = sys_uptime_bucket();
      //printf("hit bget 5\n");
      
      //printf("hit bget 6\n");
     
      // printf("get in bucket id:%d\n", bucket_id);
      // printf("after bucket head: %p\n", bucket_hit->head);
      if (old_bucket) {
          if (old_bucket_id < bucket_id) {
            release(&bucket_hit->lock);
            release(&old_bucket->lock);
          }
          else {
            release(&old_bucket->lock);
            release(&bucket_hit->lock);
          }
      }
      else {
        release(&bucket_hit->lock);
      }

      
      // release(&bucket_hit.lock);
      //printf("aha!!!\n");
      // printf("lru_buf: %d\n", lru_buf-bcache.buf);
      acquiresleep(&lru_buf->lock);
      printf("got yoaa!!\n");
      return lru_buf;
    }
  // release(&bcache.lock);
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
  printf("hit brelse\n");
  if(!holdingsleep(&b->lock))
    panic("brelse");
  releasesleep(&b->lock);
  // sys_uptime_bucket
  int bucket_id = get_bucket_id(b->dev, b->blockno);
  struct bucket *bucket_hit = &bcache.hash_buckets[bucket_id];
  acquire(&bucket_hit->lock);
  b->refcnt--;
  // struct buf * check_buf;
  if (b->refcnt == 0) {
    b->ticks = sys_uptime_bucket();

    // printf("release in bucket id:%d\n", bucket_id);
    // printf("brelse bucket head: %p\n", bucket_hit->head);
    printf("after brelse bucket head: %p\n", bucket_hit->head);
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


