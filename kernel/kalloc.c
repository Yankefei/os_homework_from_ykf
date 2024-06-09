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

// start from KERNBASE to PHYSTOP
uint8 mem_alloc_ref[USER_PAGE_NUM];
extern int d_save_page_num;

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

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
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE) {
    mem_alloc_ref[GET_USER_PAGE_NUM((uint64)p)] = 1;
    kfree(p);
  }
  // printf("total save page: %d\n", d_save_page_num);
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  int page_num = GET_USER_PAGE_NUM((uint64)pa);
  if (mem_alloc_ref[page_num] == 0)
    panic("kfree ref");

  mem_alloc_ref[page_num] --;
  if (mem_alloc_ref[page_num] > 0) {
    // printf("empty kfree...%p \n", pa);
    return;
  }

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
  d_save_page_num ++;
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r) {
    memset((char*)r, 5, PGSIZE); // fill with junk
    mem_alloc_ref[GET_USER_PAGE_NUM((uint64)r)] ++;
    d_save_page_num --;
    // if (d_save_page_num == 1000) {
    //   printf("d_save_page_num only  1000\n");
    // }
  }

  return (void*)r;
}

void addkllocref(uint64 pa) {
  if (pa % PGSIZE != 0) {
    panic("addkllocref not align");
  }
  int page_num = GET_USER_PAGE_NUM(pa);
  if (mem_alloc_ref[page_num] == 0) {
    panic("addkllocref ref");
  }
  // printf(">>>>addkllocref, page_num: %d\n", page_num);
  mem_alloc_ref[page_num] ++;
}
