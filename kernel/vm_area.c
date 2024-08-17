#include "param.h"
#include "types.h"
#include "memlayout.h"
#include "elf.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "file.h"
#include "vm_area.h"

struct {
  struct spinlock lock;
  struct vmarea vm_list[MAXVMAREA];
} vm_area;


void vmareainit(void)
{
  initlock(&vm_area.lock, "vm_area");
}

struct vmarea*
vmareaalloc(void)
{
  struct vmarea* vm;

  acquire(&vm_area.lock);
  for (vm = vm_area.vm_list; vm < vm_area.vm_list + MAXVMAREA; vm++) {
    if (vm->ref == 0) {
      vm->ref = 1;
      release(&vm_area.lock);
      return vm;
    }
  }
  release(&vm_area.lock);
  return 0;
}

void vmarearelease(struct vmarea* vm) {
  acquire(&vm_area.lock);
  if (vm->ref < 1)
    panic("vmarearelease");
  if (--vm->ref > 0) {
    release(&vm_area.lock);
    return;
  }


  release(&vm_area.lock);
}


