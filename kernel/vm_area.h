#include "fcntl.h"

struct file;

struct vmarea {
  uint64 addr;
  size_t len;
  int permission;
  int ref; // reference count
  struct file* file;  // todo
  off_t offset;
};
