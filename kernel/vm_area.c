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
#include "proc.h"

struct {
  struct spinlock lock;
  struct vmarea vm_list[MAXVMAREA];
} vm_area;


void vmareainit(void)
{
  initlock(&vm_area.lock, "vm_area");
  // 应该用上面的 lock 足够
  // for (int i = 0; i < MAXVMAREA; i++) {
  //   initlock(&vm_area.vm_list[i].lock, "vm_list_area");
  // }
}

// 设置初始状态
struct vmarea*
vmareaalloc(uint64 addr, size_t len, int flags, off_t offset)
{
  struct vmarea* vm;

  acquire(&vm_area.lock);
  for (vm = vm_area.vm_list; vm < vm_area.vm_list + MAXVMAREA; vm++) {
    if (vm->ref == 0) {
      vm->ref = 1;

      vm->addr = vm->addr_base = addr;
      vm->len = vm->len_base = len;
      vm->permission = flags;
      vm->offset = vm->base_off = offset;
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

  vm->addr = 0;
  vm->len = 0;
  vm->addr_base = 0;
  vm->len_base = 0;
  vm->permission = 0;
  vm->file = 0;
  vm->base_off = 0;
  vm->offset = 0;

  release(&vm_area.lock);
}


// Increment ref count for vmarea
struct vmarea*
vmareadup(struct vmarea *vm)
{
  acquire(&vm_area.lock);
  if(vm->ref < 1)
    panic("vmareadup");
  vm->ref++;
  release(&vm_area.lock);
  return vm;
}

int
vmareacheckscope(struct vmarea *vm, uint64 addr, size_t len) {
  int ret = 0;

  acquire(&vm_area.lock);

  // 因为在申请的时候，会申请向上取整 PGSIZE 的物理内存，所以，释放的时候，攒够一个PGSIZE 再释放
  if (len > vm->len) {
    printf("invalid len: %d, vm->len: %d\n", len, vm->len);
    return -1;
  }
  if (addr < vm->addr || vm->addr + vm->len <= addr) {
    printf("invalid addr: %p, vm->addr: %p, vm->len: %d\n",
      addr, vm->addr, vm->len);
    return -1;
  }
  if (addr + len > vm->addr + vm->len) {
    printf("invalid addr and len, addr: %p, len: %d, vm->addr: %p, vm->len: %d",
      addr, len, vm->addr, vm->len);
    return -1;
  }
  // 需要在边界开始释放内存
  if (addr != vm->addr && (addr + len) != (vm->addr + vm->len)) {
    printf("addr and len need in edge, addr: %p, len: %d, vm->addr: %p, vm->len: %d",
      addr, len, vm->addr, vm->len);
    return -1;
  }

  release(&vm_area.lock);
  return ret;
}

struct vmarea*
vmarereducescope(struct vmarea *vm, uint64 addr, size_t len) {
  acquire(&vm_area.lock);
  // 缩减内存范围
  if (addr == vm->addr) {
    vm->addr += len;
  } else {
    vm->addr -= len;
  }
  vm->len -= len;
  release(&vm_area.lock);
  return vm;
}

// 每次读取  4096个字节的长度，可能存在多次读取的情况
int
vmareaallocmemory(struct proc *p, uint64 dst) {
  printf("vmareaallocmemory\n");
  int i, found = 0;
  struct vmarea* vm;

  if (dst % PGSIZE != 0) {
    panic("vmareaallocmemory dst");
  }

  acquire(&vm_area.lock);
  acquire(&p->lock);
  for (i = 0; i < NVMAREA; i++) {
    vm = p->vm_list[i];
    if (vm && (vm->addr <= dst && vm->addr + vm->len > dst))
    {
      found = 1;
      break;
    }
  }

  if (0 == found) {
    release(&p->lock);
    release(&vm_area.lock);
    return -1;
  }

  ilock(vm->file->ip);

  uint size =  (uint)(vm->addr + vm->len - dst);
  size = size > PGSIZE ? PGSIZE: size;

  pte_t *pte;
  pte = walk(p->pagetable, dst, 0);
  if(pte == 0 || (*pte & PTE_R) != 0 || (*pte & PTE_W) != 0) {
    printf("pte is invalid...\n");
    iunlock(vm->file->ip);
    release(&p->lock);
    release(&vm_area.lock);
    return -1;
  }
  *pte |= PTE_R;
  *pte |= PTE_W;
  uint64 pa0 = PTE2PA(*pte);
  memset((void*)pa0, 0, PGSIZE); // init mem

  int ret = readi(vm->file->ip, 0, pa0, vm->offset, size);
  // 如果读取的长度小于应该映射的长度，说明文件比较小
  // 需要进行兼容处理
  if (ret != size) {
    printf("readi: size: %d, ret: %d\n", size, ret);
    //panic("vmareaallocmemory, readi");
  }
  iunlock(vm->file->ip);
  // printf("vmareaallocmemory ip: ref: %d\n", vm->file->ip->ref);
  // iunlockput(vm->file->ip);

  vm->offset = vm->base_off + ret;
  release(&p->lock);
  release(&vm_area.lock);

  return 0;
}
