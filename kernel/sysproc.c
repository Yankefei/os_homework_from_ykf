#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "sysinfo.h"

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

uint64 sys_trace(void)
{
  int mask;
  argint(0, &mask);
  struct proc* p = myproc();

  if (p->mask != 0)
    return -1;

  // acquire(&p->lock);
  p->mask = mask;
  // release(&p->lock);

  return 0;
}

int argsysinfo(uint64 sinfo_addr) {
  struct sysinfo  sys_info;
  sys_info.freemem = getkfreemem();
  sys_info.nproc = getusedprocnum();

  struct proc *p = myproc();
  // 将sys_info 这个存在于内核空间的数据，拷贝到 sinfo_addr 指向的用户空间中
  // printf("sinfo_addr: %p, sys_info: %p\n", (char*)sinfo_addr, &sys_info);
  if (copyout(p->pagetable, sinfo_addr, (char*)&sys_info, sizeof(struct sysinfo)) < 0) {
    return -1;
  }

  return 0;
}

/**
 *
 struct sysinfo {
  uint64 freemem;   // amount of free memory (bytes)
  uint64 nproc;     // number of process
 };
 */

uint64 sys_sysinfo(void) {
  uint64 sinfo = 0;
  argaddr(0, &sinfo);
  return argsysinfo(sinfo);
}
