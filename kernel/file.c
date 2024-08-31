//
// Support functions for system calls that involve file descriptors.
//

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "fs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "file.h"
#include "stat.h"
#include "proc.h"
#include "vm_area.h"

struct devsw devsw[NDEV];
struct {
  struct spinlock lock;
  struct file file[NFILE];
} ftable;

void
fileinit(void)
{
  initlock(&ftable.lock, "ftable");
}

// Allocate a file structure.
struct file*
filealloc(void)
{
  struct file *f;

  acquire(&ftable.lock);
  for(f = ftable.file; f < ftable.file + NFILE; f++){
    if(f->ref == 0){
      f->ref = 1;
      release(&ftable.lock);
      return f;
    }
  }
  release(&ftable.lock);
  return 0;
}

// Increment ref count for file f.
struct file*
filedup(struct file *f)
{
  acquire(&ftable.lock);
  if(f->ref < 1)
    panic("filedup");
  f->ref++;
  release(&ftable.lock);
  return f;
}

// Close file f.  (Decrement ref count, close when reaches 0.)
void
fileclose(struct file *f)
{
  struct file ff;

  acquire(&ftable.lock);
  if(f->ref < 1)
    panic("fileclose");
  if(--f->ref > 0){
    release(&ftable.lock);
    // 存在mmap的时候，第一次close, 到这里就退出了，不会执行下面的清理操作
    return;
  }
  ff = *f;
  f->ref = 0;
  f->type = FD_NONE;
  release(&ftable.lock);

  if(ff.type == FD_PIPE){
    pipeclose(ff.pipe, ff.writable);
  } else if(ff.type == FD_INODE || ff.type == FD_DEVICE){
    begin_op();
    // 当在 sys_unlink 里面没有清理掉的文件，unmap执行后，会在这里进行清理
    // 清理满足的条件：
    // ip->ref == 1   && ip->valid  && ip->nlink == 0
    iput(ff.ip);
    end_op();
  }
}

// Get metadata about file f.
// addr is a user virtual address, pointing to a struct stat.
int
filestat(struct file *f, uint64 addr)
{
  struct proc *p = myproc();
  struct stat st;
  
  if(f->type == FD_INODE || f->type == FD_DEVICE){
    ilock(f->ip);
    stati(f->ip, &st);
    iunlock(f->ip);
    if(copyout(p->pagetable, addr, (char *)&st, sizeof(st)) < 0)
      return -1;
    return 0;
  }
  return -1;
}

// Read from file f.
// addr is a user virtual address.
int
fileread(struct file *f, uint64 addr, int n)
{
  int r = 0;

  if(f->readable == 0)
    return -1;

  if(f->type == FD_PIPE){
    r = piperead(f->pipe, addr, n);
  } else if(f->type == FD_DEVICE){
    if(f->major < 0 || f->major >= NDEV || !devsw[f->major].read)
      return -1;
    r = devsw[f->major].read(1, addr, n);
  } else if(f->type == FD_INODE){
    ilock(f->ip);
    if((r = readi(f->ip, 1, addr, f->off, n)) > 0)
      f->off += r;
    iunlock(f->ip);
  } else {
    panic("fileread");
  }

  return r;
}

int realfilewrite(struct file *f, uint* off, uint64 addr, int n) {
  int r, ret = 0;

  // write a few blocks at a time to avoid exceeding
  // the maximum log transaction size, including
  // i-node, indirect block, allocation blocks,
  // and 2 blocks of slop for non-aligned writes.
  // this really belongs lower down, since writei()
  // might be writing a device like the console.
  int max = ((MAXOPBLOCKS-1-1-2) / 2) * BSIZE;
  int i = 0;
  while(i < n){
    int n1 = n - i;
    if(n1 > max)
      n1 = max;

    begin_op();
    ilock(f->ip);
    if ((r = writei(f->ip, 1, addr + i, *off, n1)) > 0)
      *off += r;
    iunlock(f->ip);
    end_op();

    if(r != n1){
      // error from writei
      break;
    }
    i += r;
  }

  ret = (i == n ? n : -1);

  return ret;
}

// Write to file f.
// addr is a user virtual address.
int
filewrite(struct file *f, uint64 addr, int n)
{
  int ret = 0;

  if(f->writable == 0)
    return -1;

  if(f->type == FD_PIPE){
    ret = pipewrite(f->pipe, addr, n);
  } else if(f->type == FD_DEVICE){
    if(f->major < 0 || f->major >= NDEV || !devsw[f->major].write)
      return -1;
    ret = devsw[f->major].write(1, addr, n);
  } else if(f->type == FD_INODE){
    ret = realfilewrite(f, &f->off, addr, n);
  } else {
    panic("filewrite");
  }

  return ret;
}


#if 0
// 将使用 vm_area.c 里面的 vmarereducescope 函数作为替代，因为这里
// 可能存在现成不安全的情况
// mmap munmap

// 每次读取  4096个字节的长度，可能存在多次读取的情况
int copyfilepage(struct proc *p, uint64 dst) {
  printf("copyfilepage\n");
  int i, found = 0;
  struct vmarea* vm;

  if (dst % PGSIZE != 0) {
    panic("copyfilepage dst");
  }

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
    //panic("copyfilepage, readi");
  }
  iunlock(vm->file->ip);
  // printf("copyfilepage ip: ref: %d\n", vm->file->ip->ref);
  // iunlockput(vm->file->ip);

  vm->offset = vm->base_off + ret;
  release(&p->lock);

  return 0;
}

#endif
