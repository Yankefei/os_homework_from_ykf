#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

/* Possible states of a thread: */
#define FREE        0x0
#define RUNNING     0x1
#define RUNNABLE    0x2

#define STACK_SIZE  8192
#define MAX_THREAD  4

#define PGSIZE 4096 // bytes per page
#define PGROUNDDOWN(a) (((a)) & ~(PGSIZE-1))


struct threadContext {
  uint64 era;
  uint64 esp;
  uint64 es0;
  uint64 es1;
  uint64 es2;
  uint64 es3;
  uint64 es4;
  uint64 es5;
  uint64 es6;
  uint64 es7;
  uint64 es8;
  uint64 es9;
  uint64 es10;
  uint64 es11;

  uint64 stack;        // stack*  112
};

struct thread {
  int        id;
  char       stack[STACK_SIZE]; /* the thread's stack */
  int        state;             /* FREE, RUNNING, RUNNABLE */
  struct threadContext  context;  // switch context
};
struct thread all_thread[MAX_THREAD];
struct thread *current_thread;

extern void thread_switch(uint64, uint64);
extern void copy_stack(struct thread *t);

void
thread_init(void)
{
  // main() is thread 0, which will make the first invocation to
  // thread_schedule(). It needs a stack so that the first thread_switch() can
  // save thread 0's state.
  current_thread = &all_thread[0];
  current_thread->state = RUNNING;
  for (int i = 0; i < MAX_THREAD; i++) {
    all_thread[i].id = i;
  }
}

uint64 getStackBase(struct threadContext* context) {
  uint64 t_stack_end = PGROUNDDOWN(context->esp);
  return t_stack_end;
}

/**
 * @note 为什么不能在 uthread_switch.S 中，调用这个函数呢？而需要把这个逻辑实现为汇编指令?
 *       因为如果存在函数，那么总会出现使用栈帧的时候，而这里面的操作，涉及到栈帧的更新，所以
 *       直接使用，必然会造成退出时，栈帧对应不上，执行异常的情况，所以，只能放在汇编中实现这里面
 *       的所有逻辑
*/
void copy_stack(struct thread *t) {
  // local ->  t
  // current -> local
  printf("copy_stack thread: %p\n", t);

  void* t_stack_end = (void*)PGROUNDDOWN(t->context.esp);
  printf("t: esp: %p, t_stack_end: %p\n", t->context.esp, t_stack_end);
  //memcpy(t->stack, t_stack_end, PGSIZE);
  for (uint64 i = 0; i < PGSIZE; i ++) {
    *(t->stack + i) = *((char*)t_stack_end + i);
  }

  void* c_stack_end = (void*)PGROUNDDOWN(current_thread->context.esp);
  printf("c: esp: %p, t_stack_end: %p\n", current_thread->context.esp, c_stack_end);
  if (c_stack_end == t_stack_end) {
    //memcpy(c_stack_end, current_thread->stack, PGSIZE);
    for (uint64 i = 0; i < PGSIZE; i ++) {
      *((char*)c_stack_end + i) = *(current_thread->stack + i);
    }
  } else {
    // 只设置sp的地址，为stack top
    current_thread->context.esp = (uint64)t_stack_end + PGSIZE;
  }
  printf("end copy_stack\n");
}

void 
thread_schedule(void)
{
  struct thread *t, *next_thread;

  /* Find another runnable thread. */
  next_thread = 0;
  t = current_thread + 1;
  for(int i = 0; i < MAX_THREAD; i++){
    if(t >= all_thread + MAX_THREAD)
      t = all_thread;
    if(t->state == RUNNABLE) {
      next_thread = t;
      break;
    }
    t = t + 1;
  }

  if (next_thread == 0) {
    printf("thread_schedule: no runnable threads\n");
    exit(-1);
  }

  if (current_thread != next_thread) {         /* switch threads?  */
    next_thread->state = RUNNING;
    t = current_thread;
    current_thread = next_thread;
    /* YOUR CODE HERE
     * Invoke thread_switch to switch from t to next_thread:
     * thread_switch(??, ??);
     */

    // reset t
    if (t->state != FREE)
      t->state = RUNNABLE;
    memset(t->stack, 0, sizeof(t->stack));
    // swap context
    // local ->  t
    // current ->  local

    t->context.stack = (uint64)t->stack;

    current_thread->context.stack = (uint64)current_thread->stack;
    // printf("thread_schedule  first-thread: %p, %d\n", t, t->id);
    // printf("thread_schedule second-thread: %p, %d\n", current_thread, current_thread->id);
    thread_switch((uint64)&t->context, (uint64)&current_thread->context);
  } else
    next_thread = 0;
}

void
thread_create(void (*func)())
{
  struct thread *t;

  for (t = all_thread; t < all_thread + MAX_THREAD; t++) {
    if (t->state == FREE) break;
  }
  t->state = RUNNABLE;
  // YOUR CODE HERE

  memset(&t->context, 0, sizeof(struct threadContext));
  t->context.era = (uint64)func;
  // printf("create t. era: %p. id: %d\n", t->context.era, t->id);
  thread_schedule();
}

void 
thread_yield(void)
{
  current_thread->state = RUNNABLE;
  thread_schedule();
}

volatile int a_started, b_started, c_started;
volatile int a_n, b_n, c_n;

void 
thread_a(void)
{
  int i;
  printf("thread_a started\n");
  a_started = 1;
  while(b_started == 0 || c_started == 0)
    thread_yield();
  
  for (i = 0; i < 100; i++) {
    printf("thread_a %d\n", i);
    a_n += 1;
    thread_yield();
  }
  printf("thread_a: exit after %d\n", a_n);

  current_thread->state = FREE;
  thread_schedule();
}

void 
thread_b(void)
{
  int i;
  printf("thread_b started\n");
  b_started = 1;
  while(a_started == 0 || c_started == 0)
    thread_yield();
  
  for (i = 0; i < 100; i++) {
    printf("thread_b %d\n", i);
    b_n += 1;
    thread_yield();
  }
  printf("thread_b: exit after %d\n", b_n);

  current_thread->state = FREE;
  thread_schedule();
}

void 
thread_c(void)
{
  int i;
  printf("thread_c started\n");
  c_started = 1;
  while(a_started == 0 || b_started == 0)
    thread_yield();
  
  for (i = 0; i < 100; i++) {
    printf("thread_c %d\n", i);
    c_n += 1;
    thread_yield();
  }
  printf("thread_c: exit after %d\n", c_n);

  current_thread->state = FREE;
  thread_schedule();
}

int 
main(int argc, char *argv[]) 
{
  a_started = b_started = c_started = 0;
  a_n = b_n = c_n = 0;
  thread_init();
  thread_create(thread_a);
  thread_create(thread_b);
  thread_create(thread_c);
  current_thread->state = FREE;
  // printf("main thread will set free, schedule...\n");
  thread_schedule();
  exit(0);
}
