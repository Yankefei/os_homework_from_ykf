#include "kernel/types.h"
#include "kernel/riscv.h"
#include "kernel/defs.h"
#include "kernel/param.h"



void internal_dummy_main(int argc, char *argv[])
{
    for (int i = 0; i < argc; i++)
    {
        printf("Argument %d: %s\n", i, argv[i]); 
    }
}

void demo3(void)
{
    printf("demo 3\n");
    char *args[] = {"i", "love", "trampoline.S"};
    internal_dummy_main(3, args);
}

/*
000000000000009e <sum_to>:

int sum_to(int n) {
  9e:	1141                	addi	sp,sp,-16  # sp向栈顶方向扩展16字节
  a0:	e422                	sd	s0,8(sp)     # 将s0 存在sp +8的位置  （old s0),不唯一，大多数在 sp + 0的地方
  a2:	0800                	addi	s0,sp,16   # 将sp + 16的值，保存到新s0上，
  int acc = 0;
  for (int i = 0; i <= n; i++) {
  a4:	00054d63          	bltz	a0,be <sum_to+0x20>
  a8:	0015071b          	addiw	a4,a0,1
  ac:	4781                	li	a5,0
  int acc = 0;
  ae:	4501                	li	a0,0
      acc += i;
  b0:	9d3d                	addw	a0,a0,a5
  for (int i = 0; i <= n; i++) {
  b2:	2785                	addiw	a5,a5,1
  b4:	fee79ee3          	bne	a5,a4,b0 <sum_to+0x12>
  }
  return acc;
}
  b8:	6422                	ld	s0,8(sp)          # s0 <- sp + 8
  ba:	0141                	addi	sp,sp,16        # sp <- sp + 16  退栈
  bc:	8082                	ret
  int acc = 0;
  be:	4501                	li	a0,0
  c0:	bfe5                	j	b8 <sum_to+0x1a>
*/

int sum_to(int n) {
  int acc = 0;
  for (int i = 0; i <= n; i++) {
      acc += i;
  }
  printf("test\n");
  return acc;
}

/*
00000000000000c2 <sum_then_double>:

// ra   返回地址寄存器，下一行指令的地址
// sp   栈指针，指向栈顶的位置
// s0/fp   帧指针，指向当前函数栈帧的基地址，在函数调用时被设置，通常指向栈上的一个固定位置（起始位置）
        在整个函数执行期间，帧指针保持不变

int sum_then_double(int n) {
  c2:	1141                	addi	sp,sp,-16      # sp  <-  sp - 16  开辟栈空间
  c4:	e406                	sd	ra,8(sp)         # ra  ->  sp + 8   保存返回地址，用于退出时的复原，只有调用函数时，才会保存ra
  c6:	e022                	sd	s0,0(sp)         # s0  ->  sp + 0   保存上一个栈帧的s0,     
  c8:	0800                	addi	s0,sp,16       # s0  <-  sp + 16  新的栈帧位于之前sp的位置
  int acc = sum_to(n);
  ca:	00000097          	auipc	ra,0x0
  ce:	fd4080e7          	jalr	-44(ra) # 9e <sum_to>
  acc *= 2;
  return acc;
}
  d2:	0015151b          	slliw	a0,a0,0x1
  d6:	60a2                	ld	ra,8(sp)         #  ra  <-  sp + 8   恢复 ra的地址
  d8:	6402                	ld	s0,0(sp)         #  s0  <-  sp       恢复s0
  da:	0141                	addi	sp,sp,16       #  sp  <-  sp + 16  恢复栈顶的位置
  dc:	8082                	ret

*/

int sum_then_double(int n) {
  int acc = sum_to(n);
  acc *= 2;
  return acc;
}

void main(void) {
  int i = 10;
  //int ret;
  /*ret = */  /*sum_to(i);*/
  //printf("sum_to: %d\n", ret);

  /*ret = */sum_then_double(i);
  //printf("sum_then_double: %d\n", ret);

  exit(0);
}
