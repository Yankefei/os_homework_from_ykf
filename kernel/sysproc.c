#include "types.h"
#include "riscv.h"
#include "param.h"
#include "defs.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

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
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;


  argint(0, &n);
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


// 最多4M
#define MAX_PAGE_NAME 1024

int vm_pgaccess(uint64 addr, int page_size, char* dst) {
  if (page_size > MAX_PAGE_NAME) {
    printf("page_size is overlength\n");
    return -1;
  }

  pte_t *pte;
  uint64 va0;

  struct proc *p = myproc();
  va0 = PGROUNDDOWN(addr);
  for (int i = 0; i < page_size; i++) {
    if (va0 >= MAXVA) {
      printf("%s, va0 is more than MAXVA\n", __FUNCTION__);
      return -1;
    }

    if (i > 0 && i % 8 == 0)
      dst += 1;

    pte = walk(p->pagetable, va0, 0);
    va0 += PGSIZE;

    if (!(*pte & PTE_V)) {
      continue;
    }

    if ((*pte & PTE_U) == 0) {
      printf("%s, i: %d, va0 is invalid, %d\n", __FUNCTION__, i, PTE_FLAGS(*pte));
      return -1;
    }

    if (*pte & PTE_A) {
      // printf("catch PTE_A: %d\n", i);
      *dst |= 1 << i % 8;
      *pte &= (~PTE_A);
    }
  }

  return 0;
}


#ifdef LAB_PGTBL
int
sys_pgaccess(void)
{
  // lab pgtbl: your code here.
  uint64 user_addr;
  int page_num;
  uint64 mask_addr;

  argaddr(0, &user_addr);
  argint(1, &page_num);
  argaddr(2, &mask_addr);

  char mask_buf[MAX_PAGE_NAME];
  memset(mask_buf, 0, sizeof(mask_buf));
  int ret = vm_pgaccess(user_addr, page_num, mask_buf);
  if (ret < 0)
    return -1;

  struct proc *p = myproc();
  if (copyout(p->pagetable, mask_addr, mask_buf, (page_num / 8) + 1) < 0) {
    return -1;
  }

  return 0;
}
#endif

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
