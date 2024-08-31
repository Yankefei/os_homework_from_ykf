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

#define min(a, b) ((a) < (b) ? (a) : (b))

struct {
  struct spinlock lock;
  struct vmarea vm_list[MAXVMAREA];
  struct vmareabase vm_base_list[MAXVMAREA];
} vm_area;


void vmareainit(void)
{
  initlock(&vm_area.lock, "vm_area");

  for (int i = 0; i < MAXVMAREA; i++) {
    vm_area.vm_list[i].usable = 0;
    vm_area.vm_base_list[i].ref = 0;
    // 在物理内存中申请1页, 来专门保存
    vm_area.vm_base_list[i].list_size = PGSIZE / sizeof(struct pageinfo);
    void* ptr = kalloc();
    memset(ptr, 0, PGSIZE);
    vm_area.vm_base_list[i].page_list = (struct pageinfo*)ptr;
    vm_area.vm_base_list[i].list_range_size = 0;
  }
}

// 设置初始状态
struct vmarea*
vmareaalloc(uint64 addr, size_t len, int flags, int prot, off_t offset)
{
  struct vmarea* vm;
  struct vmareabase* vm_base;
  int ret = 0;

  acquire(&vm_area.lock);

  for (vm_base = vm_area.vm_base_list; vm_base < vm_area.vm_base_list + MAXVMAREA; vm_base ++) {
    if (vm_base->ref == 0) {
      vm_base->ref = 1;
      ret = 1;
      break;
    }
  }
  if (ret == 0) {
    return 0;
  }

  for (vm = vm_area.vm_list; vm < vm_area.vm_list + MAXVMAREA; vm++) {
    if (vm->usable == 0) {
      vm->usable = 1;

      vm->addr = vm_base->addr_base = addr;
      vm->len = vm_base->len_base = len;
      vm_base->permission = flags;
      vm_base->prot = prot;
      vm_base->base_off = offset;

      vm->vm_base = vm_base;
      release(&vm_area.lock);
      return vm;
    }
  }
  release(&vm_area.lock);
  return 0;
}

struct vmarea*
vmarecopy(struct vmarea* oldvm)
{
  struct vmarea* vm;

  acquire(&vm_area.lock);

  for (vm = vm_area.vm_list; vm < vm_area.vm_list + MAXVMAREA; vm++) {
    if (vm->usable == 0) {
      vm->usable = 1;

      vm->addr = oldvm->addr;
      vm->len = oldvm->len;
      vm->vm_base = oldvm->vm_base;
      vm->vm_base->ref ++;
      release(&vm_area.lock);
      return vm;
    }
  }
  release(&vm_area.lock);
  return 0;
}

void vmarearelease(struct vmarea* vm) {
  acquire(&vm_area.lock);
  if (vm->vm_base->ref < 1)
    panic("vmarearelease");

  vm->addr = 0;
  vm->len = 0;
  vm->usable = 0;
  if (--vm->vm_base->ref > 0) {
    release(&vm_area.lock);
    return;
  }

  vm->vm_base->addr_base = 0;
  vm->vm_base->len_base = 0;
  vm->vm_base->permission = 0;
  vm->vm_base->file = 0;
  vm->vm_base->base_off = 0;
  vm->vm_base->prot = 0;

  release(&vm_area.lock);
}


// Increment ref count for vmarea
struct vmarea*
vmareadup(struct vmarea *vm)
{
  acquire(&vm_area.lock);
  if(vm->vm_base->ref < 1)
    panic("vmareadup");
  vm->vm_base->ref++;
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

// 填充 page_list 结构，一个page占用一个结构
int setpagelist(struct vmarea* vm, uint64 va) {
  // 外面需要加锁
  // acquire(&vm_area.lock);
  for (int i = 0; i < vm->vm_base->list_size; i++) {
    struct pageinfo* page = &vm->vm_base->page_list[i];
    if (page->addr != 0) {
      if (page->addr == va) {
        page->ref++;
        return page->ref;
      }
    } else {
      // printf("setpagelist: %p, i: %d\n", va, i);
      page->addr = va;
      page->ref = 1;
      vm->vm_base->list_range_size ++;
      break;
    }
  }

  // release(&vm_area.lock);
  return 0;
}

int pagefilewriteback(struct vmarea* vm, uint64 va, size_t len) {
  int n, write_len;
  uint64 addr, file_off;
  struct file *f = vm->vm_base->file;
  if(f->writable == 0)
    return -1;

  acquire(&vm_area.lock);
  // printf("pagefilewriteback va: %p, len: %d\n", va, len);
  for (int i = 0; i < vm->vm_base->list_range_size; i++) {
    struct pageinfo page = vm->vm_base->page_list[i];
    // printf("page: i: %d, %p\n", i, page.addr);
    //    va---------------len
    //
    // |    A    |   B  |   C    |
    // 根据va的位置来划分
    // 计算当前page需要写入的地址和长度
    if (va > page.addr && (va <= page.addr + PGSIZE))  {  //A
      addr = va;
      write_len = min(len, PGSIZE - (va - page.addr));
    }  else if (page.addr >= va && page.addr < va + len) {  // B or C
      addr = page.addr;
      write_len = (va + len) >= (page.addr + PGSIZE) ? PGSIZE : (va + len - page.addr);
    } else {
      continue;
    }

    // 计算当前位置距离映射起始点的偏移量，跟进这个来写入对应偏移量的文件中
    file_off = addr - vm->vm_base->addr_base;
    release(&vm_area.lock);
    // 不能加任何lock
    // 写回文件，略去是否为脏页的校验
    // printf("pagefilewriteback: addr: %p, len :%d  off: %d\n", addr, write_len, file_off);
    n = realfilewrite(f, (uint*)&file_off, addr, write_len);
    if (n != write_len) {
      printf("sys_munmap, filewrite failed, n: %d, len: %d\n", n, write_len);

      return -1;
    }
    acquire(&vm_area.lock);
  }
  release(&vm_area.lock);

  return 0;
}


void remappagelist(struct proc* p, struct proc* np, struct vmarea* vm) {
  pte_t *pte;
  for (int i = 0; i < vm->vm_base->list_range_size; i++) {
    struct pageinfo* page = &vm->vm_base->page_list[i];
    if (page->addr != 0) {
      if ((pte = walk(p->pagetable, page->addr, 0)) == 0) {
        panic("remappagelist walk addr");
      }
      int page_flags = PTE_FLAGS(*pte);
      if(mappages(np->pagetable, page->addr, PGSIZE, (uint64)PTE2PA(*pte), page_flags) != 0){
        panic("remappagelist np process addr");
      }
      // 重新映射，也需要将物理内存的ref引用计数递增
      // 这里主要用于fork的场景
      page->ref ++;
    }
  }
}

void cleanpagelistmemory(struct proc* p, struct vmarea* vm) {
  acquire(&vm_area.lock);

  for (int i = 0; i < vm->vm_base->list_range_size; i++) {
    struct pageinfo* page = &vm->vm_base->page_list[i];
    if (page->addr != 0) {
      if (--page->ref > 0) {
        // 这里是虽然不能清理掉物理内存，但是需要接触叶子指针 pte_t 的绑定关系
        // 所以最后一个参数为0
        uvmunmap(p->pagetable, page->addr, 1, 0);
        continue;
      }

      // printf("clean: addr: %p, i: %d\n", page->addr, i);
      // 这里除了接触 pte_t的绑定关系，还需要清理物理内存，所以最后一个参数为1
      uvmunmap(p->pagetable, page->addr, 1, 1);
      page->addr = 0;
    }
  }
  // 这里不能修改vm_base里面的内容，很容易错的地方
  // vm->vm_base->list_range_size = 0;

  release(&vm_area.lock);
}

// 每次申请 4096个字节的物理地址，会有空余出现
int
vmareaallocmemory(struct proc *p, uint64 dst) {
  printf("vmareaallocmemory  dst: %p\n", dst);
  int i, found = 0;
  struct vmarea* vm;

  if (dst >= MAXVA) {
    return -1;
  }

  uint64 va = PGROUNDDOWN(dst);

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

  // 物理内存申请策略：
  // 1. 一次申请一个PGSIZE大小的空间, 如果原始mmap的大小超过 PGSIZE, 则记录当前申请的地址信息，以便后面单独释放
  // 2. 如果父进程已经申请了，那么则无需申请, setpagelist 函数可以判断当前已经映射物理内存
  //    的区域, 如果申请了，则仅增加page的映射关系，如果每增加，则完整申请一个内存快
  if (setpagelist(vm, va) == 0) {
    if((uvmalloc(p->pagetable, va, va + PGSIZE, PTE_W)) == 0) {
      release(&p->lock);
      release(&vm_area.lock);
      return -1;
    }
  }

  // 计算需要从文件读取的信息
  uint size = (uint)(vm->addr + vm->len - dst);
  size = size > PGSIZE ? PGSIZE : size;
  // 需要判断从文件的哪个偏移量开始读取
  uint diffa = dst - vm->vm_base->addr_base;

  ilock(vm->vm_base->file->ip);
  // printf("readi: addr: %p, len: %d, diffa: %d\n", va, size, diffa);
  int ret = readi(vm->vm_base->file->ip, 1, va, vm->vm_base->base_off + diffa, size);
  // 如果读取的长度小于应该映射的长度，说明文件比较小
  // 需要进行兼容处理
  if (ret != size) {
    printf("readi: size: %d, ret: %d\n", size, ret);;
  }
  iunlock(vm->vm_base->file->ip);
  // printf("vmareaallocmemory ip: ref: %d\n", vm->file->ip->ref);

  release(&p->lock);
  release(&vm_area.lock);

  return 0;
}
