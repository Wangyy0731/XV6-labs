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

#define NBUCKET 13 // the count of hash table's buckets
#define HASH(blockno) (blockno % NBUCKET)

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

struct
{
  struct spinlock lock;
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  // struct buf head;
  int size;                       // 引用计数，每个buffer当中所拥有的cache数
  struct buf buckets[NBUCKET];    // 整个cache所对应的bucket
  struct spinlock locks[NBUCKET]; // bucket所对应的锁
  struct spinlock hashlock;       // hash表对应的全局锁
} bcache;

extern uint ticks;

void binit(void)
{
  struct buf *b;
  bcache.size = 0;
  initlock(&bcache.lock, "bcache");
  initlock(&bcache.hashlock, "bcache_hash");

  //初始化有关锁
  for (int i = 0; i < NBUCKET; ++i)
  {
    initlock(&bcache.locks[i], "bcache_bucket");
  }

  // Create linked list of buffers
  // bcache.head.prev = &bcache.head;
  // bcache.head.next = &bcache.head;
  for (b = bcache.buf; b < bcache.buf + NBUF; b++)
  {
    // b->next = bcache.head.next;
    // b->prev = &bcache.head;
    initsleeplock(&b->lock, "buffer");
    // bcache.head.next->prev = b;
    // bcache.head.next = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf *
bget(uint dev, uint blockno)
{
  struct buf *b;
  int index = HASH(blockno);
  struct buf *pre, *minb = 0, *minpre;
  uint mintimestamp;
  int i;

  acquire(&bcache.locks[index]);

  // 把buf分配到buffer当中
  for (b = bcache.buckets[index].next; b; b = b->next)
  {
    if (b->dev == dev && b->blockno == blockno)
    {
      b->refcnt++;
      release(&bcache.locks[index]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  //检查是否还有空闲块可供使用
  acquire(&bcache.lock);
  if (bcache.size < NBUF)
  {
    b = &bcache.buf[bcache.size++];
    release(&bcache.lock);
    b->dev = dev;
    b->blockno = blockno;
    b->valid = 0;
    b->refcnt = 1;
    b->next = bcache.buckets[index].next;
    bcache.buckets[index].next = b;
    release(&bcache.locks[index]);
    acquiresleep(&b->lock);
    return b;
  }
  release(&bcache.lock);
  release(&bcache.locks[index]);

  //采用LRU算法进行置换
  acquire(&bcache.hashlock);
  for (i = 0; i < NBUCKET; ++i)
  {
    mintimestamp = -1;
    acquire(&bcache.locks[index]);
    for (pre = &bcache.buckets[index], b = pre->next; b; pre = b, b = b->next)
    {
      // 寻找当前被分配的块的位置
      if (index == HASH(blockno) && b->dev == dev && b->blockno == blockno)
      {
        b->refcnt++;
        release(&bcache.locks[index]);
        release(&bcache.hashlock);
        acquiresleep(&b->lock);
        return b;
      }
      if (b->refcnt == 0 && b->timestamp < mintimestamp)
      {
        minb = b;
        minpre = pre;
        mintimestamp = b->timestamp;
      }
    }
    //找到一个没有被用过的块
    if (minb)
    {
      minb->dev = dev;
      minb->blockno = blockno;
      minb->valid = 0;
      minb->refcnt = 1;
      // 如果该块在其他bucket当中，则将其挪到该bucket当中
      if (index != HASH(blockno))
      {
        minpre->next = minb->next; // 移走该块
        release(&bcache.locks[index]);
        index = HASH(blockno); // 找到对应的bucket
        acquire(&bcache.locks[index]);
        minb->next = bcache.buckets[index].next; // 将该块移到对应的bucket当中
        bcache.buckets[index].next = minb;
      }
      release(&bcache.locks[index]);
      release(&bcache.hashlock);
      acquiresleep(&minb->lock);
      return minb;
    }
    release(&bcache.locks[index]);
    if (++index == NBUCKET)
    {
      index = 0;
    }
  }
  panic("bget: no buffers");
  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  /*
  for(b = bcache.head.prev; b != &bcache.head; b = b->prev){
    if(b->refcnt == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  */
}

// Return a locked buf with the contents of the indicated block.
struct buf *
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if (!b->valid)
  {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void bwrite(struct buf *b)
{
  if (!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void brelse(struct buf *b)
{
  int index;
  if (!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  index = HASH(b->blockno);

  acquire(&bcache.locks[index]);
  b->refcnt--;
  if (b->refcnt == 0)
  {
    // no one is waiting for it.
    // b->next->prev = b->prev;
    // b->prev->next = b->next;
    // b->next = bcache.head.next;
    // b->prev = &bcache.head;
    // bcache.head.next->prev = b;
    // bcache.head.next = b;
    b->timestamp = ticks;
  }

  release(&bcache.locks[index]);
}

void bpin(struct buf *b)
{

  int index = HASH(b->blockno);
  acquire(&bcache.locks[index]);
  b->refcnt++;
  release(&bcache.locks[index]);
}

void bunpin(struct buf *b)
{

  int index = HASH(b->blockno);
  acquire(&bcache.locks[index]);
  b->refcnt--;
  release(&bcache.locks[index]);
}
