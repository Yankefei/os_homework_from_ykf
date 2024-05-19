#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int g(int x) {
  return x+3;
}

int f(int x) {
  return g(x);
}

void main(void) {

  // unsigned int b = 0x00646c72;
  // // 0010 0111
  // // 0011 0110
  // // 0010 0110
  // printf("H%x Wo%s", 57616, &b); //  HEll0 World

  printf("%d %d\n", f(8)+1, 13);

/**
  printf("x=%d y=%d\n", 3);
  4c:	458d                	li	a1,3
  4e:	00000517          	auipc	a0,0x0
  52:	7da50513          	addi	a0,a0,2010 # 828 <malloc+0x102>
  56:	00000097          	auipc	ra,0x0
  5a:	612080e7          	jalr	1554(ra) # 668 <printf>
*/
  printf("x=%d y=%d\n", 3);  //  a2 寄存器的历史值

  exit(0);
}
