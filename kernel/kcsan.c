#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "proc.h"
#include "defs.h"

//
// Race detector using gcc's thread sanitizer. It delays all stores
// and loads and monitors if any other CPU is using the same address.
// If so, we have a race and print out the backtrace of the thread
// that raced and the thread that set the watchpoint.
//

/**
当启用 ThreadSanitizer 后，编译器在每个内存访问点插入适当的函数调用。例如：
- 对于读取一个 int 型变量（假设是 4 字节），编译器会插入 __tsan_read4。
- 对于写入一个 int 型变量，编译器会插入 __tsan_write4。
这些函数被插入到汇编代码中，当程序运行时，它们会被调用。ThreadSanitizer 的运行时库会捕获这些调用，
并记录下每个内存访问的详细信息，包括访问的线程、内存地址、访问类型（读或写）、访问的大小等。
*/

//
// To run with kcsan:
// make clean
// make KCSAN=1 qemu
//

// The number of watch points.
#define NWATCH (NCPU)

// 不管那意味着什么
// The number of cycles to delay stores, whatever that means on qemu.
//#define DELAY_CYCLES 20000
#define DELAY_CYCLES 200000

#define MAXTRACE 20


// 如何确定栈帧返回值ra的地址？
// 当前栈帧的返回值没有地方保存，直接获取即可
// 之前栈帧的返回值，可以跟进栈帧来进行推算
/*
// 自己实现的trace版本，逻辑基本一致
void backtrace() {
  uint64 fp, last_fp, last_ra, page_aligned;

  // 因为backtrace应该是定位到调用函数的行，而不是下一行
  // 所以地址应该 - 4， 但是测试发现，并不能完全匹配，故放弃
  printf("%p\n", r_ra());
  fp = r_fp();
  page_aligned = PGROUNDUP(fp);
  // 因为backtrace里面存在printf的函数调用，所以上一个栈帧
  // 的偏移量不会是8，只能是16
  fp = *(uint64*)(fp - 16);

  // 只能小于，不能等于, 因为第一个堆栈 fp == page_aligned
  while(fp < page_aligned) {
    // last frame
    last_ra = *(uint64*)(fp - 8);
    printf("%p\n", last_ra);
    last_fp = *(uint64*)(fp - 16);
    fp = last_fp;
  }
}
*/

int
trace(uint64 *trace, int maxtrace)
{
  uint64 i = 0;

  // 调用了push_off ， 最后再调用pop_off ，
  // 是为了创造出一个暂时关闭了 devie_interrupts enabled （SSTATUS_SIE 被清理）的空间，
  // 之后会对现场进行还原
  // todo  打印backtrace信息不能中断？

  push_off();
  
  uint64 fp = r_fp();
  uint64 ra, low = PGROUNDDOWN(fp) + 16, high = PGROUNDUP(fp);

  // 如果 fp 的最后三位是 0，那么 fp 地址就是 8 的倍数，表示 fp 是8字节对齐的
  while(!(fp & 7) && fp >= low && fp < high){
    ra = *(uint64*)(fp - 8);
    fp = *(uint64*)(fp - 16);
    trace[i++] = ra;
    if(i >= maxtrace)
      break;
  }

  pop_off();
  
  return i;
}

struct watch {
  uint64 addr;
  int write;
  int race;
  uint64 trace[MAXTRACE];
  int tracesz;
};
  
struct {
  struct spinlock lock;
  struct watch points[NWATCH];
  int on;
} tsan;

static struct watch*
wp_lookup(uint64 addr)
{
  for(struct watch *w = &tsan.points[0]; w < &tsan.points[NWATCH]; w++) {
    if(w->addr == addr) {
      return w;
    }
  }
  return 0;
}

// 安装，并获取当前位置的堆栈，找不到空闲points则异常？
static int
wp_install(uint64 addr, int write)
{
  for(struct watch *w = &tsan.points[0]; w < &tsan.points[NWATCH]; w++) {
    if(w->addr == 0) {
      w->addr = addr;
      w->write = write;
      w->tracesz = trace(w->trace, MAXTRACE);
      return 1;
    }
  }
  panic("wp_install");
  return 0;
}

static void
wp_remove(uint64 addr)
{
  for(struct watch *w = &tsan.points[0]; w < &tsan.points[NWATCH]; w++) {
    if(w->addr == addr) {
      w->addr = 0;
      w->tracesz = 0;
      return;
    }
  }
  panic("remove");
}

static void
printtrace(uint64 *t, int n)
{
  int i;
  
  for(i = 0; i < n; i++) {
    printf("%p\n", t[i]);
  }
}

/**
 * 打印 当前的实时堆栈，以及历史 watch里面的堆栈
*/
static void
race(char *s, struct watch *w) {
  uint64 t[MAXTRACE];
  int n;
  
  n = trace(t, MAXTRACE);
  printf("== race detected ==\n");
  printf("backtrace for racing %s\n", s);
  printtrace(t, n);
  printf("backtrace for watchpoint:\n");
  printtrace(w->trace, w->tracesz);
  printf("==========\n");
}

// cycle counter
// 函数 r_cycle() 的功能是读取当前的 CPU 周期计数器值，并将其作为 uint64 类型的值返回。
// 这个函数使用内联汇编来直接访问 CPU 寄存器，以获取周期计数器的值。
static inline uint64
r_cycle()
{
  uint64 x;
  asm volatile("rdcycle %0" : "=r" (x) );
  return x;
}

static void delay(void) __attribute__((noinline));
static void delay() {
  uint64 stop = r_cycle() + DELAY_CYCLES;
  uint64 c = r_cycle();
  while(c < stop) {
    c = r_cycle();
  }
}

static void
kcsan_read(uint64 addr, int sz)
{
  struct watch *w;
  
  acquire(&tsan.lock);
  // 作用：
  // 在读取的时候，如果发现当前有其他进程或线程在写入，则打印race信息
  if((w = wp_lookup(addr)) != 0) {
    if(w->write) {
      race("load", w);
    }
    release(&tsan.lock);
    return;
  }
  release(&tsan.lock);
}

static void
kcsan_write(uint64 addr, int sz)
{
  struct watch *w;
  
  acquire(&tsan.lock);
  // 发现addr 之前在watch中，则打印两边的堆栈
  // 作用：
  // 在写入的时候，如果发现当前有其他进程或线程在写入，则打印race信息
  if((w = wp_lookup(addr)) != 0) {
    race("store", w);
    release(&tsan.lock);
  }

  // no watchpoint; try to install one
  if(wp_install(addr, 1)) {

    release(&tsan.lock);

    // XXX maybe read value at addr before and after delay to catch
    // races of unknown origins (e.g., device).

    // delay 一段时间，那么有可能有其他线程，进程来访问
    delay(); 

    acquire(&tsan.lock);

    wp_remove(addr);
  }
  release(&tsan.lock);
}

// tsan.on will only have effect with "make KCSAN=1"
void
kcsaninit(void)
{
  initlock(&tsan.lock, "tsan");
  tsan.on = 1;
  __sync_synchronize();
}

//
// Calls inserted by compiler into kernel binary, except for this file.
// 因为上面的很多函数，都被下面函数所调用
//

// - 作用：用于初始化 ThreadSanitizer 的运行时环境。
// 在程序开始执行之前，这个函数会设置好必要的内部数据结构和监控机制。
void
__tsan_init(void)
{
}

// read
void
__tsan_read1(uint64 addr)
{
  if(!tsan.on)
    return;
  // kcsan_read(addr, 1);
}

void
__tsan_read2(uint64 addr)
{
  if(!tsan.on)
    return;
  kcsan_read(addr, 2);
}

void
__tsan_read4(uint64 addr)
{
  if(!tsan.on)
    return;
  kcsan_read(addr, 4);
}

void
__tsan_read8(uint64 addr)
{
  if(!tsan.on)
    return;
  kcsan_read(addr, 8);
}

void
__tsan_read_range(uint64 addr, uint64 size)
{
  if(!tsan.on)
    return;
  kcsan_read(addr, size);
}


// write
void
__tsan_write1(uint64 addr)
{
  if(!tsan.on)
    return;
  // kcsan_write(addr, 1);
}

void
__tsan_write2(uint64 addr)
{
  if(!tsan.on)
    return;
  kcsan_write(addr, 2);
}

void
__tsan_write4(uint64 addr)
{
  if(!tsan.on)
    return;
  kcsan_write(addr, 4);
}

void
__tsan_write8(uint64 addr)
{
  if(!tsan.on)
    return;
  kcsan_write(addr, 8);
}

void
__tsan_write_range(uint64 addr, uint64 size)
{
  if(!tsan.on)
    return;
  kcsan_write(addr, size);
}

// atomic
void
__tsan_atomic_thread_fence(int order)
{
  __sync_synchronize();
}

uint32
__tsan_atomic32_load(uint *ptr, uint *val, int order)
{
  uint t;
  __atomic_load(ptr, &t, __ATOMIC_SEQ_CST);
  return t;
}

void
__tsan_atomic32_store(uint *ptr, uint val, int order)
{
  __atomic_store(ptr, &val, __ATOMIC_SEQ_CST);
}

// 作用：这些函数用于跟踪函数调用的进入和退出。它们帮助 ThreadSanitizer 维护调用栈信息
// 以便在检测到问题时提供更准确的错误报告

// We don't use this
void
__tsan_func_entry(uint64 pc)
{
}

// We don't use this
void
__tsan_func_exit(void)
{
}


