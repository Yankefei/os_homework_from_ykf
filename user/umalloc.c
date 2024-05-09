#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/param.h"

// Memory allocator by Kernighan and Ritchie,
// The C programming Language, 2nd ed.  Section 8.7.

typedef long Align;

union header {
  struct {
    union header *ptr;
    uint size;
  } s;
  Align x;
};

typedef union header Header;

static Header base;
static Header *freep;

// 注意，ap指针前面的header，只需要size有效就可以了
void
free(void *ap)
{
  Header *bp, *p;

  bp = (Header*)ap - 1;  /* point to block header */
  // 寻找待插入节点的位置
  // bp >p   && pb < p->s.ptr   -- 是确认 pb 是否在p，以及p->str 的范围内

  // 如果一开始没有任何节点，p 和 p->s.ptr 是一个值
  for(p = freep; !(bp > p && bp < p->s.ptr); p = p->s.ptr)
    // p >= p->s.ptr 就是指向前面节点的情况，以及初始节点的情况
    // 而且 p 和 p->s.ptr 就是整个申请区间的边界了。
    // 只有没在这个范围内的，才会直接break
    if(p >= p->s.ptr && (bp > p || bp < p->s.ptr))
      break;  /* freed block at start or end of arena */

  if(bp + bp->s.size == p->s.ptr){    /* join to upper nbr */
    bp->s.size += p->s.ptr->s.size;
    bp->s.ptr = p->s.ptr->s.ptr;
  } else
    bp->s.ptr = p->s.ptr;

  // 这里对bp->s.ptr 的取值，是上面进行的赋值
  if(p + p->s.size == bp){            /* join to lower nbr */
    p->s.size += bp->s.size;
    p->s.ptr = bp->s.ptr;
  } else
    p->s.ptr = bp;

  // 上一次释放的定点位置
  freep = p;
}

static Header*
morecore(uint nu)
{
  char *p;
  Header *hp;

  if(nu < 4096)
    nu = 4096;
  p = sbrk(nu * sizeof(Header));
  if(p == (char*)-1)
    return 0;
  hp = (Header*)p;
  hp->s.size = nu;
  free((void*)(hp + 1));      //  add 到空闲list 中
  return freep;
}

void*
malloc(uint nbytes)
{
  Header *p, *prevp;
  uint nunits;

  // sizeof(Header): 16
  // 需要额外多申请一个Header的空间，用于保存头部信息
  // 0byte, uints为1
  // 1byte, uints 为2
  // 2bytes, uints为2
  // 3, 2
  // ...
  // 16, 2
  // 17, 3
  // 18, 3
  // ...
  // 32, 3
  // 33, 4
  // ...
  nunits = (nbytes + sizeof(Header) - 1)/sizeof(Header) + 1;
  if((prevp = freep) == 0){
    base.s.ptr = freep = prevp = &base;
    base.s.size = 0;
  }
  for(p = prevp->s.ptr; ; prevp = p, p = p->s.ptr){
    if(p->s.size >= nunits){
      if(p->s.size == nunits)
        prevp->s.ptr = p->s.ptr;
      else {
        p->s.size -= nunits;
        p += p->s.size;
        p->s.size = nunits;
      }
      freep = prevp;
      return (void*)(p + 1);
    }
    if(p == freep)
      if((p = morecore(nunits)) == 0)
        return 0;
  }
}
