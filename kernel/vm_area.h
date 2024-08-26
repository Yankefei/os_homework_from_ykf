#include "fcntl.h"

struct file;

struct vmarea {
  uint64 addr;    // 随实际映射区域变化
  size_t len;

  uint64 addr_base;  // 基础映射的区域
  size_t len_base;

  int permission;
  int ref; // reference count, for fork
  struct file* file;  // todo
  off_t base_off;  // 文件的基础映射起始位置
  off_t offset;  // 会持续递增，随file
};
