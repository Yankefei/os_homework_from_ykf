#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"
#include "kernel/stat.h"
#include "kernel/fs.h"

// 注意进行释放fd
int get_file_stat(char* path, struct stat* st, int* fd_v) {
  int fd;
  if((fd = open(path, O_RDONLY)) < 0){
    fprintf(2, "get_file_stat: cannot open %s\n", path);
    return -1;
  }

  if (fstat(fd, st) < 0) {
    fprintf(2, "get_file_stat: cannot stat %s\n", path);
    close(fd);
    return -1;
  }

  *fd_v = fd;
  return 0;
}

// 递归调用
// 只在判定为 dir的时候，才进入这个调用中
// fd 为 dir的fd
void find_dir_recursion(char *path, int fd, char* target_name) {
  char buf[512], *p;
  struct dirent de;

  // 想在buf中装下 path 和 DIRSIZ
  if(strlen(path) + 1 + DIRSIZ + 1 > sizeof buf){
    printf("ls: path too long: %s\n", path);
    return;
  }
  strcpy(buf, path);

  if (strcmp(path, target_name) == 0) {
    printf("%s\n", path);
  }

  p = buf+strlen(buf);
  *p++ = '/';

  while(read(fd, &de, sizeof(de)) == sizeof(de)) {
    // printf("list: file_dir: %s, path: %s\n", de.name, path);
    // inum为0，表示这是一个空的DIR,没有被使用
    if(de.inum == 0)
      continue;

    if (strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0) {
      continue;
    }

    memmove(p, de.name, DIRSIZ);
    p[DIRSIZ] = 0;

    struct stat st;
    int fd_new;
    if (get_file_stat(buf, &st, &fd_new) == 0) {
      if (st.type == T_DIR) {
        find_dir_recursion(buf, fd_new, target_name);
      } else {
        if (strcmp(p, target_name) == 0) {
          printf("%s\n", buf);
        }
      }
      close(fd_new);
    }
  }
}

int main(int argc, char *argv[]) {

  // find . b
  if (argc != 3) {
    fprintf(2, "find <path> <filename> \n");
    exit(1);
  }

  struct stat st;
  int fd;

  if (get_file_stat(argv[1], &st, &fd) == 0) {
    if (st.type == T_DIR) {
      find_dir_recursion(argv[1], fd, argv[2]);
    } else {
      fprintf(2, "invalid type of path, need dir\n");
    }
    close(fd);
  }

  exit(0);
}
