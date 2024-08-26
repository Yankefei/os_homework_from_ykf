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
  // 对齐地址， 不对齐也没有任何而关系
  acquire(&p->lock);
  raddr = p->mmap_base;
  release(&p->lock);

  // 没有设置  R W 等权限
  // 直接从mmap映射的虚拟地址开始递增
  if(growmmapmem(len, PTE_U) < 0)
    return -1;

  struct vmarea* vm_area = vmareaalloc(raddr, len, flags, prot, offset);
  if (vm_area == 0)
    panic("vmareaalloc");

  acquire(&p->lock);
  int i;
  for (i = 0; i < NVMAREA; i ++) {
    if (p->vm_list[i] == 0) {
      break;
    }
  }

  if (p->vm_list[i] != 0) {
    release(&p->lock);
    panic("vm_list empty");
  }

  //increase the file's reference
  struct file* f = p->ofile[fd];
  if(f == 0) {
    panic("ofile");
  }

  if (prot & PROT_WRITE) {
    // 在可写状态下，当文件不可写，且 mmap为共享的状态下，认为是异常的
    if (!f->writable && !(flags & MAP_PRIVATE)) {
      goto failed;
    }
  }

  vm_area->vm_base->file = f;
  
  filedup(f);
  // printf("sys_mmap: ip: ref: %d\n", f->ip->ref);

  p->vm_list[i] = vm_area;
  // 底层  mmap_base
  release(&p->lock);

  return raddr;

failed:
  vmarearelease(vm_area);
  release(&p->lock);
  // growproc has lock
  growmmapmem(-len, 0);
  return -1;
}

/*
int munmap(void *addr, size_t len);

assume that it will either unmap at the start, or at the end, or the whole region
(but not punch a hole in the middle of a region).

*/
uint64
sys_munmap(void) {
  uint64  addr;
  size_t len;
  int n;
  struct vmarea* vm;

  argaddr(0, &addr);   // assume 0
  argaddr(1, &len);

  struct proc* p = myproc();
  acquire(&p->lock);

  int i;
  for (i = 0; i < NVMAREA; i ++) {
    if (p->vm_list[i] != 0 && p->vm_list[i]->addr == addr) {
      break;
    }
  }

  if (i == NVMAREA) {
    panic("vm_list NVMAREA");
  }

  vm = p->vm_list[i];

  release(&p->lock);

  if (vmareacheckscope(vm, addr, len) != 0) {
    goto failed;
  }

  switch(vm->vm_base->permission) {
    case MAP_PRIVATE:
      break;
    case MAP_SHARED:
      if (vm->vm_base->prot & PROT_WRITE) {
        // 写回文件，略去是否为脏页的校验
        n = filewrite(vm->vm_base->file, addr, len);
        if (n != len) {
          printf("sys_munmap, filewrite failed, n: %d, len: %d\n", n, len);
          goto failed;
        }
      }
    break;
    default:
      panic("invalid permission");
  }

  vmarereducescope(vm, addr, len);

  // 方案1：
  // 回收内存，还是等待彻底不用之后再释放？等待用完统一释放物理页吧，这样简单一些
  // 目前看只能在程序退出的时候，再一起清理，因为如果这个地方清理，会导致内存不连续，退出会异常
  // 进程回收的时候，会按顺序将所有申请的内存一起回收，所以暂时不能提前回收映射的内存
  // 方案2：
  // 支持动态回收
  if (vm->len == 0) {
    
    if (vm->vm_base->ref == 1) {
      // printf("uvmmmapdealloc. old: %p, new: %p\n", vm->addr_base + vm->len_base, vm->addr_base);
      uvmmmapdealloc(p->pagetable, vm->vm_base->addr_base + vm->vm_base->len_base, vm->vm_base->addr_base);

      // 不能加 p->lock 锁
      fileclose(vm->vm_base->file);
    }

    vmarearelease(vm);

    acquire(&p->lock);
    p->vm_list[i] = 0;
    release(&p->lock);
  }

  return 0;

failed:
  // release(&p->lock);

  return -1;
}
