#include "kernel/types.h"
#include "user/user.h"

typedef long Align;

// 存在自动对齐的现象
union header {
  struct {
    union header *ptr;
    uint size;
  } s;
  Align x;
};

typedef union header Header;

union header header_val;

struct {
  union header *ptr;  // 8
  uint size;          // 4
} s_t;                // 16


void malloc_test(uint nbytes) {

  uint nunits;
  nunits = (nbytes + sizeof(Header) - 1)/sizeof(Header) + 1;
  printf("nbytes: %d, nunits: %d\n", nbytes, nunits);
}

int main(int argc, char* argv[]) {

#if 1
  int * ptr = 0;
  printf("sizeof ptr: %d, sizeof long: %d sizeof header: %d\n",
        sizeof(ptr), sizeof(long), sizeof(union header));
  // sizeof ptr: 8, sizeof long: 8 sizeof header: 16

  // sizeof s_t: 16, sizeof uint: 4
  printf("sizeof s_t: %d, sizeof uint: %d\n", sizeof(s_t), sizeof(uint));

  // sizeof header.x: 8, header.s: 16
  printf("sizeof header.x: %d, header.s: %d\n", sizeof(header_val.x), sizeof(header_val.s));

#endif

#if 0

  int a;
  char b;
  long c; 

  // 编译失败
  printf("a: %d, b: %d, c: %d\n", a, b, c);

#endif

  for (int i = 0; i < 34; i ++) {
    malloc_test(i);
  }

  return 0;
}