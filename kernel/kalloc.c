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

// after test,data,bss section
extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

/**
 * 设计方法：使用unused_size 来维护剩余的元素，如果用完，那么就申请其他cpu分配的元素
 * 释放的时候，根据指针位置，判断应该归还给哪个freelist.
 * 如果freelist和当前的cpu_id不符合，则需要加锁
*/
struct MemInfo{
  struct spinlock lock;
  struct run *freelist;
  char name[16];
  //
  uint64 begin_addr;
  uint64 end_addr;
  int total_size;
};

struct {
  struct MemInfo mem_info[NCPU];
} kalloc_struct;

void
kinit()
{
  uint64 start_p, end_p;
  start_p = PGROUNDUP((uint64)end);
  end_p = PHYSTOP;

  uint64 total_mem = (uint64)end_p - (uint64)start_p;
  if (0 != total_mem % PGSIZE) {
    panic("total_mem");
  }

  int total_mem_pkg = (int)(total_mem / PGSIZE);
  int remainder = total_mem_pkg % NCPU;
  int average_pkg = ((int)total_mem_pkg - remainder) / NCPU;

// 32735, 3, 8183, 4
// init, total_size: 8183, begin: 0x0000000080021000, end: 0x0000000082018000
// init, total_size: 8183, begin: 0x0000000082018000, end: 0x000000008400f000
// init, total_size: 8183, begin: 0x000000008400f000, end: 0x0000000086006000
// init, total_size: 8186, begin: 0x0000000086006000, end: 0x0000000088000000

// 32736, 0, 10912, 3
// init, total_size: 10912, begin: 0x0000000080020000, end: 0x0000000082ac0000
// init, total_size: 10912, begin: 0x0000000082ac0000, end: 0x0000000085560000
// init, total_size: 10912, begin: 0x0000000085560000, end: 0x0000000088000000
  printf("%d, %d, %d, %d\n", total_mem_pkg, remainder, average_pkg, SMP_SIZE);

  // 填充除freelist之外的基本字段
  for (int i = 0; i < NCPU; i++) {
    struct MemInfo* mem_info = &kalloc_struct.mem_info[i];
    int ret = snprintf(mem_info->name, sizeof (mem_info->name), "kmem_%d", i);
    mem_info->name[ret] = '\0';
    initlock(&mem_info->lock, mem_info->name);
    // printf("%s\n", mem_info->name);
    mem_info->begin_addr = start_p;
    if (i == NCPU - 1) {
     mem_info->total_size = average_pkg + remainder;
    } else {
     mem_info->total_size = average_pkg;
    }
    mem_info->end_addr = start_p + (uint64)mem_info->total_size * PGSIZE;
    start_p = mem_info->end_addr;
    printf("init, total_size: %d, begin: %p, end: %p\n",
      mem_info->total_size, mem_info->begin_addr, mem_info->end_addr);
  }

  // 初始化free_list
  for (int i = 0; i < NCPU; i++) {
    struct MemInfo* mem_info = &kalloc_struct.mem_info[i];
    freerange((void *)mem_info->begin_addr, (void*)mem_info->end_addr);
  }
}

/**
The main challenge will be to deal with the case in which one CPU's free list is empty,
but another CPU's list has free memory; in that case, the one CPU must "steal" part of the other CPU's free list. 
Stealing may introduce lock contention, but that will hopefully be infrequent.

 *
PHYSTOP    0x88000000    < ---  end

Free memory

kernel data              <  ----   start (end[])

kernel text

KERNABASE  0x80000000L

*/

// NCPU

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

struct MemInfo* getMemInfo(void* pa) {
  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  struct MemInfo* info = &kalloc_struct.mem_info[0];
  for (; info <= &kalloc_struct.mem_info[NCPU - 1]; info ++) {
    if ((uint64)pa < info->end_addr) {
      return info;
    }
  }
  panic("get_mem_info");
}


// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  struct MemInfo* info = getMemInfo(pa);

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&info->lock);
  r->next = info->freelist;
  info->freelist = r;
  release(&info->lock);
}

int
cpuidsafe()
{
  push_off();
  int id = r_tp();
  pop_off();
  return id;
}


// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)  // todo
{
  struct run *r;

  int id = cpuidsafe();
  int read_id;
  for (int i = id; i < id + NCPU; i++) {
    read_id = i % NCPU;

    struct MemInfo* info = &kalloc_struct.mem_info[read_id];

    acquire(&info->lock);
    r = info->freelist;
    if(r)
      info->freelist = r->next;
    release(&info->lock);
    if (r) break;
  }

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
