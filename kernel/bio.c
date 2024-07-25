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

/**
Here are some hints:

1. Read the description of the block cache in the xv6 book (Section 8.1-8.3).

2. It is OK to use a fixed number of buckets and not resize the hash table dynamically. Use a prime(质数)
number of buckets (e.g., 13) to reduce the likelihood(可能性) of hashing conflicts.

3. (Searching in the hash table for a buffer and allocating an entry for that buffer when the buffer is not found) must be atomic.
   在hash table中查找一个buffer, 以及当buffer没找到时，申请一个buffer节点   整个过程需要是原子的

4. Remove the list of all buffers ( bcache.head etc.) and don't implement LRU. With this change brelse
doesn't need to acquire the bcache lock. In bget you can select any block that has refcnt == 0 instead
of the least-recently used one.

5. You probably won't be able to atomically check for a cached buf and (if not cached) find an unused
buf; you will likely have to drop(减少) all locks and start from scratch(从头开始) if the buffer isn't in the cache. It is OK
to serialize finding an unused buf in bget (i.e., the part of bget that selects a buffer to re-use when a
lookup misses in the cache).

您可能无法自动检查缓存的 buf 并（如果没有缓存）找到未使用的 buf；如果缓冲区不在缓存中，您可能必须放弃所有锁并从头开始。
序列化查找 bget 中未使用的 buf 是可以的（即，当缓存中的查找未命中时，bget 的一部分会选择一个缓冲区来重新使用）。

6. Your solution might need to hold two locks in some cases; for example, during eviction you may need
to hold the bcache lock and a lock per bucket. Make sure you avoid deadlock.

7. When replacing a block, you might move a struct buf from one bucket to another bucket, because
the new block hashes to a different bucket. You might have a tricky case(棘手场景): the new block might hash to
the same bucket as the old block. Make sure you avoid deadlock in that case.

替换块时，您可能会将 struct buf 从一个存储桶移动到另一个存储桶，因为新块会散列到不同的存储桶。您可能会遇到一个棘手的情况：新块可能会散列到
与旧块相同的存储桶。确保在这种情况下避免死锁。

8. Some debugging tips: implement bucket locks but leave the global bcache.lock acquire/release at the
beginning/end of bget to serialize the code. Once you are sure it is correct without race conditions,
remove the global locks and deal with concurrency issues. You can also run make CPUS=1 qemu to test
with one core.

9. Use xv6's race detector to find potential races (see above how to use the race detector).
*/


#if 1

// finish, pass test yankefei 2024-07-25, 不易呀

#define BUCKET_SIZE  13

// default size: 5
#define BUCKET_ELEMENT_SIZE  (NBUF / 13)



/**
策略：当散列到某一个bucket的时候，首先从 buf数组中申请节点，然后用head来组装为链表
     如果当前的buf数组申请满了，那么就从相邻的bucket中申请节点，同时本bucket的head进行指向，直到所有节点都被用光为止

    释放是相同的过程，从head链表中脱离开来，成为一个单独的个体，可以被周围所有bucket的head链表再次索引进去
*/
struct BucketInfo {
  struct buf buf[BUCKET_ELEMENT_SIZE];
  // 这个lock不仅锁住 head的链表，而且还要锁住 buf数组
  struct spinlock lock;
  struct buf* head;   // 链表的header
  int list_num;
};

struct {
  struct BucketInfo  bucket[BUCKET_SIZE];
  // struct spinlock lock;    // lock the buf list, just for test
} bcache;

void
binit(void)
{
  for (int i = 0; i < BUCKET_SIZE; i++) {
    struct BucketInfo* bucket = &bcache.bucket[i];
    initlock(&bucket->lock, "bcache.bucket");

    for (int j = 0; j < BUCKET_ELEMENT_SIZE; j++) {
      bucket->buf[j].next = 0;
      bucket->buf[j].bucket_index = i;
      bucket->buf[j].head_index = -1;
      bucket->buf[j].refcnt = 0;
      bucket->buf[j].hash_num = 0;
    }
    bucket->head = 0;
    bucket->list_num = 0;
  }
}

uint64 gethashnum(uint val1, uint val2) {
  uint64 ret_val;
  ret_val = ((uint64)val1 << 32) | val2;
  return ret_val;
}

/**
 * 加锁准则：1. 如果多个函数都要多个锁的话，那么加锁的顺序必须保持一致
 *             先加head 再加 index
 *          2. 如果在一个函数中， 出现了先加锁，再解锁，然后再加锁的操作
 *              那么可能出现，两个进程先后进入第一个临界区，然后再一起进入第二个临界区
 *              需要注意数据的一致性
 *          3.  __sync_synchronize 在acquire release里面有，所以可以不加
*/

void listpushback(struct BucketInfo* head_bucket, struct buf *b) {
  struct buf* head = head_bucket->head;
  if (head == 0) {
    head_bucket->head = b;
  } else {
    while(head->next) {
      head = head->next;
    }
    head->next = b;
  }
  head_bucket->list_num ++;
}

// 如果出现hash冲突，则需要往后 ++, 需要边界，则从头开始，保证所有bucket都遍历一次
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  uint64 hash_num = gethashnum(dev, blockno);
  int index = hash_num % BUCKET_SIZE;
  // 首先进行查找
  struct BucketInfo* head_bucket = &bcache.bucket[index];
  struct BucketInfo* index_bucket;
  acquire(&head_bucket->lock);
  for (b = head_bucket->head; b != 0; b = b->next) {
    index_bucket = &bcache.bucket[b->bucket_index];
    if (b->head_index != b->bucket_index) {
      acquire(&index_bucket->lock);
    }
    if (b->dev == dev && b->blockno == blockno && hash_num == b->hash_num) {
      b->refcnt ++;

      if (b->head_index != b->bucket_index) {
        release(&index_bucket->lock);
      }
      release(&head_bucket->lock);
      acquiresleep(&b->lock);
      return b;
    }
    if (b->head_index != b->bucket_index) {
      release(&index_bucket->lock);
    }
  }

  // 如果找不到
  // 1. 第一步：现寻找一个可用的节点
  // 2. 尾插到原来的head链表中
  int read_id_t;
  for (int i = index; i < index + BUCKET_SIZE; i++) {
    read_id_t = i % BUCKET_SIZE;
    index_bucket = &bcache.bucket[read_id_t];
    if (read_id_t != index) {
      acquire(&index_bucket->lock);
    }
    for (int j = 0; j < BUCKET_ELEMENT_SIZE; j++) {
      b = &index_bucket->buf[j];
      // 找到后，对refcnt等字段进行赋值，然后跳出
      if(b->refcnt == 0) {
        b->dev = dev;
        b->blockno = blockno;
        b->valid = 0;
        b->refcnt = 1;
        b->hash_num = hash_num;
        b->next = 0;
        b->head_index = index;

        listpushback(head_bucket, b);

        if (read_id_t != index) {
          release(&index_bucket->lock);
        }
        release(&head_bucket->lock);
        acquiresleep(&b->lock);
        return b;
      }
    }
    if (read_id_t != index) {
      release(&index_bucket->lock);
    }
  }

  release(&head_bucket->lock);  // 整个函数解锁

  panic("bget: no buffers");
  return b;
}

// release 有可能重复调用
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  if (b->head_index == -1) {
    panic("release head_index");
    return;
  }

  struct BucketInfo* index_bucket, *head_bucket;
  int double_lock = 0;
  head_bucket = &bcache.bucket[b->head_index];
  index_bucket = &bcache.bucket[b->bucket_index];
  acquire(&head_bucket->lock);
  if (b->head_index != b->bucket_index) {
    double_lock = 1;
    acquire(&index_bucket->lock);
  }

  if (b->refcnt > 1) {
    b->refcnt--;

    if (double_lock) {
      release(&index_bucket->lock);
    }
    release(&head_bucket->lock);
    return;
  }

  struct buf* head = head_bucket->head;
  if (head == b) {
    head_bucket->head = head->next;
  } else {
    while(head && head->next != b) {
      head = head->next;
    }
    if (!head)
      panic("brelse empty buf");
    head->next = head->next->next;
  }

  b->valid = 0;
  b->dev = 0;
  b->next = 0;
  b->hash_num = 0;
  b->blockno = 0;
  b->refcnt --;
  b->head_index = -1;
  head_bucket->list_num --;
  if (b->refcnt != 0) {
    printf("warn: refcnt: %d, bucket_index: %d. %p, num: %d\n", b->refcnt, b->bucket_index, b->hash_num, head_bucket->list_num);
    panic("release refcnt");
  }

  if (double_lock) {
    release(&index_bucket->lock);
  }
  release(&head_bucket->lock);
}

// 能否直接访问 b->head_index ? 可以，因为目前整个buf还被 buf->lock 的sleep lock保护着
void
bpin(struct buf *b) {
  if (b->head_index == -1)
    panic("bpin head_index");

  struct BucketInfo* head_bucket, *index_bucket;
  head_bucket = &bcache.bucket[b->head_index];
  index_bucket = &bcache.bucket[b->bucket_index];
  acquire(&head_bucket->lock);
  if (b->bucket_index != b->head_index) {
    acquire(&index_bucket->lock);
  }
  b->refcnt++;
  if (b->bucket_index != b->head_index) {
    release(&index_bucket->lock);
  }
  release(&head_bucket->lock);
}

// refcnt -- 后，必须仍然 > 0, 否则release的逻辑就会有问题
void
bunpin(struct buf *b) {
  if (b->head_index == -1/* || b->refcnt == 1*/)
    panic("bunpin head_index");

  struct BucketInfo* head_bucket, *index_bucket;
  head_bucket = &bcache.bucket[b->head_index];
  index_bucket = &bcache.bucket[b->bucket_index];
  acquire(&head_bucket->lock);
  if (b->bucket_index != b->head_index) {
    acquire(&index_bucket->lock);
  }
  b->refcnt--;
  if (b->bucket_index != b->head_index) {
    release(&index_bucket->lock);
  }
  release(&head_bucket->lock);
}

#endif


#if 0
# 第一个版本，通过test0, test1, 对于test2, 会遇到偶现的失败，todo  2024-07-19


#define BUCKET_SIZE  13

#define BUCKET_ELEMENT_SIZE  5



/**
策略：当散列到某一个bucket的时候，首先从 buf数组中申请节点，然后用head来组装为链表
     如果当前的buf数组申请满了，那么就从相邻的bucket中申请节点，同时本bucket的head进行指向，直到所有节点都被用光为止

    释放是相同的过程，从head链表中脱离开来，成为一个单独的个体，可以被周围所有bucket的head链表再次索引进去
*/
struct BucketInfo {
  struct buf buf[BUCKET_ELEMENT_SIZE];
  // 这个lock不仅锁住 head的链表，而且还要锁住 buf数组
  struct spinlock lock;
  struct buf* head;   // 链表的header
  int list_num;
};

struct {
  struct BucketInfo  bucket[BUCKET_SIZE];
} bcache;

void
binit(void)
{
  for (int i = 0; i < BUCKET_SIZE; i++) {
    struct BucketInfo* bucket = &bcache.bucket[i];
    initlock(&bucket->lock, "bcache.bucket");

    for (int j = 0; j < BUCKET_ELEMENT_SIZE; j++) {
      bucket->buf[j].next = 0;
      bucket->buf[j].bucket_index = i;
      bucket->buf[j].head_index = -1;
      bucket->buf[j].refcnt = 0;
      bucket->buf[j].hash_num = 0;
    }
    bucket->head = 0;
    bucket->list_num = 0;
  }
}

uint64 gethashnum(uint val1, uint val2) {
  uint64 ret_val;
  ret_val = ((uint64)val1 << 32) | val2;
  return ret_val;
}

// 如果出现hash冲突，则需要往后 ++, 需要边界，则从头开始，保证所有bucket都遍历一次
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  uint64 hash_num = gethashnum(dev, blockno);
  int index = hash_num % BUCKET_SIZE;
  // 首先进行查找
  struct BucketInfo* bucket = &bcache.bucket[index];
  acquire(&bucket->lock);
  for (b = bucket->head; b != 0; b = b->next) {
    if (b->dev == dev && b->blockno == blockno && hash_num == b->hash_num) {
      b->refcnt ++;
      __sync_synchronize();
      // if (b->blockno == 46 || b->blockno == 33) {
      //   printf("bget, ++ b->refcnt: %d,  %d %p, num: %d\n", b->refcnt, b->bucket_index, b->hash_num, bucket->list_num);
      //   for (struct buf* begin = bucket->head; begin != 0; begin = begin->next) {
      //     printf(">> elem: %p, head: %d, index: %d, refcnt: %d\n",
      //       begin->hash_num, begin->head_index, begin->bucket_index, begin->refcnt);
      //   }
      // }
      release(&bucket->lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  release(&bucket->lock);  // 解锁恢复

  // 如果找不到
  // 1. 第一步：现寻找一个可用的节点
  // 2. 尾插到原来的head链表中
  int read_id_t;
  for (int i = index; i < index + BUCKET_SIZE; i++) {
    read_id_t = i % BUCKET_SIZE;
    bucket = &bcache.bucket[read_id_t];
    acquire(&bucket->lock);
    for (int j = 0; j < BUCKET_ELEMENT_SIZE; j++) {
      b = &bucket->buf[j];
      if(b->refcnt == 0) {
        break;
      }
    }
    // 找到后，对refcnt等字段进行赋值，然后跳出
    if (b->refcnt == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      b->hash_num = hash_num;
      b->next = 0;
      __sync_synchronize();
      release(&bucket->lock);  // 访问结束，进行解锁
      break;
    }
    release(&bucket->lock);  // 当b->refcnt > 0 后, 则可以暂时脱离锁的保护
    b = 0;  // 表示b仍然没有被找到
  }

  if (0 == b)
    panic("bget: no buffers");
  else {
    // 获取到b节点后，再加锁进行处理，这里的锁可能和buf所在bucket的固定索引不同
    bucket = &bcache.bucket[index];
    acquire(&bucket->lock);
    b->head_index = index;
    struct buf* head = bucket->head;
    if (head == 0) {
      bucket->head = b;
    } else {
      while(head->next) {
        head = head->next;
      }
      head->next = b;
    }
    bucket->list_num ++;
    // if (b->blockno == 46 || b->blockno == 33) {
    //   printf("bget, new b->refcnt: %d, %d %p, num: %d\n", b->refcnt, b->bucket_index, b->hash_num, bucket->list_num);
    //   for (struct buf* begin = bucket->head; begin != 0; begin = begin->next) {
    //     printf(">> elem: %p, head: %d, index: %d, refcnt: %d\n",
    //       begin->hash_num, begin->head_index, begin->bucket_index, begin->refcnt);
    //   }
    // }
    release(&bucket->lock);
    acquiresleep(&b->lock);
  }
  return b;
}

// release 有可能重复调用
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  if (b->head_index == -1) {
    panic("release head_index");
    return;
  }

  struct BucketInfo* index_bucket, *head_bucket;
  int double_lock = 0;

  head_bucket = &bcache.bucket[b->head_index];
  index_bucket = &bcache.bucket[b->bucket_index];
  if (b->head_index != b->bucket_index) {
    double_lock = 1;
    acquire(&index_bucket->lock);
  }
  acquire(&head_bucket->lock);

  if (b->refcnt > 1) {
    b->refcnt--;
    __sync_synchronize();
    // if (b->blockno == 46 || b->blockno == 33) {
    //   printf("relse, -- b->refcnt: %d,  %d %p, num: %d\n", b->refcnt, b->bucket_index, b->hash_num, head_bucket->list_num);
    //   for (struct buf* begin = head_bucket->head; begin != 0; begin = begin->next) {
    //     printf(">> elem: %p, head: %d, index: %d, refcnt: %d\n",
    //       begin->hash_num, begin->head_index, begin->bucket_index, begin->refcnt);
    //   }
    // }
    release(&head_bucket->lock);
    if (double_lock) {
      release(&index_bucket->lock);
    }
    return;
  }

  struct buf* head = head_bucket->head;
  if (head->next == 0) {
    if (head != b) {
      panic("release head not b");
    }
    head_bucket->head = 0;
  } else if (head == b) {
    head_bucket->head = head->next;
  } else {
    while(head->next && head->next != b) {
      head = head->next;
    }
    // 先剥离，然后再解锁
    head->next = b->next;
  }
  
  b->dev = 0;
  b->next = 0;
  // b->hash_num = 0;
  b->refcnt --;
  b->head_index = -1;
  head_bucket->list_num --;
  __sync_synchronize();
  if (b->refcnt != 0) {
    printf("warn: refcnt: %d, bucket_index: %d. %p, num: %d\n", b->refcnt, b->bucket_index, b->hash_num, head_bucket->list_num);

    // panic("release refcnt");
  } else {
    // if (b->blockno == 46 || b->blockno == 33) {
    //   printf("release refcnt to %d, bucket_index: %d. %p, num: %d\n", b->refcnt, b->bucket_index, b->hash_num, head_bucket->list_num);
    //   for (struct buf* begin = head_bucket->head; begin != 0; begin = begin->next) {
    //     printf(">> elem: %p, head: %d, index: %d, refcnt: %d\n",
    //       begin->hash_num, begin->head_index, begin->bucket_index, begin->refcnt);
    //   }
    // }
  }
  b->hash_num = 0;
  b->blockno = 0;
  release(&head_bucket->lock);
  if (double_lock) {
    release(&index_bucket->lock);
  }
}

// 方案还是需要确定，能否直接访问 b->head_index， 因为这个值也是在不断地变化
void
bpin(struct buf *b) {
  if (b->head_index == -1)
    panic("bpin head_index");

  struct BucketInfo* head_bucket, *index_bucket;
  head_bucket = &bcache.bucket[b->head_index];
  index_bucket = &bcache.bucket[b->bucket_index];
  if (b->bucket_index != b->head_index) {
    acquire(&index_bucket->lock);
  }
  acquire(&head_bucket->lock);
  b->refcnt++;
  __sync_synchronize();
  // printf("bpin: b->refcnt: %d,  %d %p\n", b->refcnt, b->bucket_index, b->hash_num);
  release(&head_bucket->lock);
  if (b->bucket_index != b->head_index) {
    release(&index_bucket->lock);
  }
}

// refcnt -- 后，必须仍然 > 0, 否则release的逻辑就会有问题
void
bunpin(struct buf *b) {
  if (b->head_index == -1/* || b->refcnt == 1*/)
    panic("bunpin head_index");
  struct BucketInfo* head_bucket, *index_bucket;
  head_bucket = &bcache.bucket[b->head_index];
  index_bucket = &bcache.bucket[b->bucket_index];
  if (b->bucket_index != b->head_index) {
    acquire(&index_bucket->lock);
  }
  acquire(&head_bucket->lock);
  b->refcnt--;
  __sync_synchronize();
  // printf("bunpin: b->refcnt: %d,  %d %p\n", b->refcnt, b->bucket_index, b->hash_num);
  release(&head_bucket->lock);
  if (b->bucket_index != b->head_index) {
    release(&index_bucket->lock);
  }
}

#endif

#if 0

struct {
  struct spinlock lock;    // lock the buf list
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  struct buf head;
} bcache;

void
binit(void)
{
  struct buf *b;

  initlock(&bcache.lock, "bcache");

  // Create linked list of buffers
  bcache.head.prev = &bcache.head;
  bcache.head.next = &bcache.head;
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    initsleeplock(&b->lock, "buffer");
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  acquire(&bcache.lock);

  // Is the block already cached?
  for(b = bcache.head.next; b != &bcache.head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.lock);
      // It is safe for bget to acquire the buffer’s sleep-lock outside of the bcache.lock critical
      // section, since the non-zero b->refcnt prevents the buffer from being re-used for a different
      // disk block. 
      // 当release bcache.lock 和 acquiresleep b->lock 之间，b->refcnt非零 可以避免映射到同一块缓存buf.(主要是用于下面初次申请的场景)
      // 而当获取相同的块时，如这个代码块，那么可以不用保护，因为 b->dev == dev && b->blockno == blockno 本身就标志他们是同样的缓存
      // The sleep-lock protects reads and writes of the block’s buffered content, while the
      // bcache.lock protects information about which blocks are cached.
      // 那么下面的 b->lock 是做什么的？ 是用于保护buf里面的data数据，避免不通进程同时读写。
      // 这也解释了为什么 brelse 先releasesleep了 b->lock, 然后才是获取bcache.lock， 因为上一个进程已经读写完毕，
      // 当前的进程可以继续读写了。
      // 也说明了两个锁保护的维护完全不同。
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
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
  panic("bget: no buffers");
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  acquire(&bcache.lock);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }
  
  release(&bcache.lock);
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


#endif

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
