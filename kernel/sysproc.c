#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
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
  backtrace();

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
目前的策略是，如果sigalarm被调用后，那么之前的所有痕迹都要被清理掉
包括sigreturn
*/
uint64
sys_sigalarm(void)
{
  uint64 vp;
  int ticks;
  argint(0, &ticks);
  argaddr(1, &vp);

  struct sig* sig_ptr = &myproc()->sig;

  sig_ptr->alarm_handle = (void(*)())vp;
  sig_ptr->ticks_interval = ticks;
  sig_ptr->ticks_pass = 0;
  sig_ptr->ticks_trigger = 0;

  return 0;
}

/*
目前的策略是，alarm_handle 执行的时候，如果比较耗时，则下一次触发时，会从
return 后，开始计数

return的时刻，放在 sys_sigreturn里面，而不是在usertrap里面!
*/
uint64
sys_sigreturn(void)
{
  struct proc *p = myproc();
  struct sig* sig_ptr = &p->sig;
  // 只有发生过alarm_handle调用才可以触发
  if (sig_ptr->ticks_trigger) {
    memmove(&(p->trapframe->epc), p->trapframe->cache_buf, 264);
    sig_ptr->ticks_trigger = 0;
  }

  return 0;
}
