//
// test program for the alarm lab.
// you can modify this file for testing,
// but please make sure your kernel
// modifications pass the original
// versions of these tests.
//

#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/riscv.h"
#include "user/user.h"

void test0();
void test1();
void test2();
void test3();
void periodic();
void slow_handler();
void dummy_handler();

int
main(int argc, char *argv[])
{
  test0();
  test1();
  test2();
  test3();
  exit(0);
}

volatile static int count;

void
periodic()
{
  count = count + 1;
  printf("alarm!\n");
  sigreturn();
}

// tests whether the kernel calls
// the alarm handler even a single time.
void
test0()
{
  int i;
  printf("test0 start\n");
  count = 0;
  sigalarm(2, periodic);
  for(i = 0; i < 1000*500000; i++){
    if((i % 1000000) == 0)
      write(2, ".", 1);
    if(count > 0)
      break;
  }
  sigalarm(0, 0);
  if(count > 0){
    printf("test0 passed\n");
  } else {
    printf("\ntest0 failed: the kernel never called the alarm handler\n");
  }
}

void __attribute__ ((noinline)) foo(int i, int *j) {
  if((i % 2500000) == 0) {
    write(2, ".", 1);
  }
  *j += 1;
}

//
// tests that the kernel calls the handler multiple times.
//
// tests that, when the handler returns, it returns to
// the point in the program where the timer interrupt
// occurred, with all registers holding the same values they
// held when the interrupt occurred.
//
void
test1()
{
  int i;
  int j;

  printf("test1 start\n");
  count = 0;
  j = 0;
  sigalarm(2, periodic);
  for(i = 0; i < 500000000; i++){
    if(count >= 10)
      break;
    foo(i, &j);
  }
  if(count < 10){
    printf("\ntest1 failed: too few calls to the handler\n");
  } else if(i != j){
    // the loop should have called foo() i times, and foo() should
    // have incremented j once per call, so j should equal i.
    // once possible source of errors is that the handler may
    // return somewhere other than where the timer interrupt
    // occurred; another is that that registers may not be
    // restored correctly, causing i or j or the address ofj
    // to get an incorrect value.
    printf("\ntest1 failed: foo() executed fewer times than it was called\n");
  } else {
    printf("test1 passed\n");
  }
}

//
// tests that kernel does not allow reentrant alarm calls.
void
test2()
{
  int i;
  int pid;
  int status;

  printf("test2 start\n");
  if ((pid = fork()) < 0) {
    printf("test2: fork failed\n");
  }
  if (pid == 0) {
    count = 0;
    sigalarm(2, slow_handler);
    for(i = 0; i < 1000*500000; i++){
      if((i % 1000000) == 0)
        write(2, ".", 1);
      if(count > 0)
        break;
    }
    if (count == 0) {
      printf("\ntest2 failed: alarm not called\n");
      exit(1);
    }
    exit(0);
  }
  // 为什么进入wait后，test1()的 sigalarm 没有继续触发了？
  // 因为wait 会进入内核态，是不会触发 usertrap 的
  wait(&status);
  if (status == 0) {
    printf("test2 passed\n");
  }
}

void
slow_handler()
{
  count++;
  printf("alarm!\n");
  if (count > 1) {
    printf("test2 failed: alarm handler called more than once\n");
    exit(1);
  }
  for (int i = 0; i < 1000*500000; i++) {
    asm volatile("nop"); // avoid compiler optimizing away loop
  }
  // 增加超长耗时的打印，也不会触发
  // for(int i = 0; i < 1000*500000; i++){
  //   if((i % 100000) == 0)
  //     write(2, "0", 1);
  // }

  sigalarm(0, 0);
  sigreturn();
}

//
// dummy alarm handler; after running immediately uninstall
// itself and finish signal handling
void
dummy_handler()
{
  sigalarm(0, 0);
  sigreturn();
}

//
// tests that the return from sys_sigreturn() does not
// modify the a0 register
// 所以这个test也提示我们，需要从sigreturn直接切换epc, 而不是让它返回, 如果返回，a0 肯定为0
void
test3()
{
  uint64 a0;

  sigalarm(1, dummy_handler);
  printf("test3 start\n");

  /*
  这行汇编指令将寄存器 a5 的高20位清零。lui 指令用于加载一个立即数到寄存器的高20位，这里加载的值是 0
  */
  asm volatile("lui a5, 0");
  /*
  addi a0, a5, 0xac：将寄存器 a5 的值加上立即数 0xac 并将结果存储到寄存器 a0 中。由于之前 a5 的值是 0，所以 a0 将被赋值为 0xac（即十六进制的172）。
  : : : "a0"：告诉编译器在这条指令后 a0 寄存器的值可能被改变。
  */
  asm volatile("addi a0, a5, 0xac" : : : "a0");
  for(int i = 0; i < 500000000; i++)
    ;
  asm volatile("mv %0, a0" : "=r" (a0) );

  /*
  上面的语句：
  使用汇编指令将寄存器 a5 的高20位清零。
  将寄存器 a5 的值（0）加上立即数 0xac（十六进制172），结果存储在寄存器 a0 中。
  执行一个无操作的空循环5亿次，引入延迟。
  将寄存器 a0 的值复制到C变量 a0 中。
  */
  if(a0 != 0xac)
    printf("test3 failed: register a0 changed\n");
  else
    printf("test3 passed\n");
}
