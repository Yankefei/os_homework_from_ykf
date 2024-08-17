#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "proc.h"
#include "vm_area.h"
#include "file.h"

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return wait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int n;

  argint(0, &n);
  addr = myproc()->sz;
  if(growproc(n, PTE_W | PTE_R | PTE_U) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  argint(0, &n);
  if(n < 0)
    n = 0;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(killed(myproc())){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

/*
void *mmap(void *addr, size_t len, int prot, int flags,
int fd, off_t offset);

针对文件的申请操作，放在 page-fault 里面执行

//mmap returns that address, or 0xffffffffffffffff if it fails.

// It's OK if processes that map the same MAP_SHARED file do not share physical pages.
*/

/*
1. 找一个未被使用区域的进程空间
2. 向进程列表添加一个 VMA结构
3. add file's ref
*/

uint64
sys_mmap(void) {
  uint64  addr, raddr;
  size_t len;
  int prot, flags, fd;
  off_t offset;

  argaddr(0, &addr);   // assume 0
  argaddr(1, &len);
  argint(2, &prot);
  argint(3, &flags);
  argint(4, &fd);
  argaddr(5, (uint64*)&offset);   // assume 0

  // 使用 kalloc来申请，可以考餐 sys_sbrk 函数的实现
  // 直接用 sbrk 来进行申请
  struct proc* p = myproc();
  acquire(&p->lock);
  raddr = p->sz;
  release(&p->lock);

  if(growproc(len, 0) < 0)
    return -1;

  struct vmarea* vm_area = vmareaalloc();
  if (vm_area == 0)
    panic("vmareaalloc");

  vm_area->addr = raddr;
  vm_area->len = len;
  vm_area->ref = 1;   // use in fork
  vm_area->permission = 0;
  vm_area->offset = offset;

  int i;
  for (i = 0; i < NVMAREA; i ++) {
    if (p->vm_list[i] == 0) {
      break;
    }
  }

  if (p->vm_list[i] != 0) {
    panic("vm_list empty");
  }

  p->vm_list[i] = vm_area;

  //increase the file's reference
  if(p->ofile[fd] == 0) {
    panic("ofile");
  }

  vm_area->file = p->ofile[fd];
  p->ofile[fd]->ref ++;

  return raddr;
}

/*
int munmap(void *addr, size_t len);

assume that it will either unmap at the start, or at the end, or the whole region
(but not punch a hole in the middle of a region).


*/
uint64
sys_munmap(void) {
  return 0;
}
