#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/riscv.h"
#include "kernel/fcntl.h"
#include "kernel/spinlock.h"
#include "kernel/sleeplock.h"
#include "kernel/fs.h"
#include "kernel/file.h"
#include "user/user.h"

#define fail(msg) do {printf("FAILURE: " msg "\n"); failed = 1; goto done;} while (0);
static int failed = 0;

static void testsymlink(void);
static void concur(void);
static void cleanup(void);

/**

Hints:
1. First, create a new system call number for symlink, add an entry to user/usys.pl, user/user.h, 
   and implement an empty sys_symlink in kernel/sysfile.c.

2. Add a new file type ( T_SYMLINK ) to kernel/stat.h to represent a symbolic link.

3. Add a new flag to kernel/fcntl.h, ( O_NOFOLLOW ), that can be used with the open system call. 
   Note that flags passed to open are combined using a bitwise OR operator, so your new flag should
   not overlap with any existing flags. This will let you compile user/symlinktest.c once you add it to the Makefile.

4. Implement the symlink(target, path) system call to create a new symbolic link at path that refers to target. 
   Note that target does not need to exist for the system call to succeed. 
   [实现 symlink(target, path) 系统调用，在 path 处创建一个指向 target 的新符号链接。
   请注意，目标不需要存在，系统调用也能成功。]
   You will need to choose somewhere to store the target path of a symbolic link, for example, in the inode's data blocks.
   [放在 inode的data block中]
   symlink should return an integer representing success (0) or failure (-1) similar to link and unlink .

   ln -s 命令可以创建一个指向不存在文件或目录的软链接。具体来说，软链接的目标文件或目录不需要在创建软链接时存在。
   这是因为软链接只是包含路径信息的特殊文件，而不检查目标路径是否有效
   因为软链接可以提前设置好，以便稍后创建对应的文件或目录

5. Modify the open system call to handle the case where the path refers to a symbolic link. If the file does not exist,
   open must fail. When a process specifies O_NOFOLLOW in the flags to open , open should open the symlink
   (and not follow the symbolic link).

6. If the linked file is also a symbolic link, you must recursively follow it until a non-link file is reached. 
   If the links form a cycle, you must return an error code. You may approximate this by returning an error code
   if the depth of links reaches some threshold (e.g., 10).

7. Other system calls (e.g., link and unlink) must not follow symbolic links; these system calls operate on the
   symbolic link itself.

8. You do not have to handle symbolic links to directories for this lab.
   [您不必处理此实验的目录的符号链接]

//////////////////

做法：1. 符号链接的目录信息还是需要进行处理
     2. 具体的链接位置，放在对应文件的 inode data block 中
     3. 处理循环链接的情况

     4. 需要支持创建和删除的过程
     5. 软链接的名称不能和对应目录或文件的名称一样
     6. 对同一个文件，不能连续创建2个名称一样的软链接
     7. 如果目标文件不存在，那么将创建一个空的inode节点

*/

int
main(int argc, char *argv[])
{
  cleanup();
  testsymlink();
  concur();
  exit(failed);
}

static void
cleanup(void)
{
  unlink("/testsymlink/a");
  unlink("/testsymlink/b");
  unlink("/testsymlink/c");
  unlink("/testsymlink/1");
  unlink("/testsymlink/2");
  unlink("/testsymlink/3");
  unlink("/testsymlink/4");
  unlink("/testsymlink/z");
  unlink("/testsymlink/y");
  unlink("/testsymlink");
}

// stat a symbolic link using O_NOFOLLOW
static int
stat_slink(char *pn, struct stat *st)
{
  int fd = open(pn, O_RDONLY | O_NOFOLLOW);
  if(fd < 0)
    return -1;
  if(fstat(fd, st) != 0)
    return -1;
  return 0;
}

static void
testsymlink(void)
{
  int r, fd1 = -1, fd2 = -1;
  char buf[4] = {'a', 'b', 'c', 'd'};
  char c = 0, c2 = 0;
  struct stat st;
    
  printf("Start: test symlinks\n");

  mkdir("/testsymlink");

  fd1 = open("/testsymlink/a", O_CREATE | O_RDWR);
  if(fd1 < 0) fail("failed to open a");

  r = symlink("/testsymlink/a", "/testsymlink/b");   // b ->  a
  if(r < 0)
    fail("symlink b -> a failed");

  if(write(fd1, buf, sizeof(buf)) != 4)
    fail("failed to write to a");

  if (stat_slink("/testsymlink/b", &st) != 0)
    fail("failed to stat b");
  if(st.type != T_SYMLINK)
    fail("b isn't a symlink");

  fd2 = open("/testsymlink/b", O_RDWR);
  if(fd2 < 0)
    fail("failed to open b");
  read(fd2, &c, 1);
  if (c != 'a')
    fail("failed to read bytes from b");

  unlink("/testsymlink/a");                    // b -> (a)
  if(open("/testsymlink/b", O_RDWR) >= 0)
    fail("Should not be able to open b after deleting a");

  r = symlink("/testsymlink/b", "/testsymlink/a");    //  a  ->  b -> (a)
  if(r < 0)
    fail("symlink a -> b failed");

  r = open("/testsymlink/b", O_RDWR);   // 需要打开循环
  if(r >= 0)
    fail("Should not be able to open b (cycle b->a->b->..)\n");
  
  r = symlink("/testsymlink/nonexistent", "/testsymlink/c");  // c -> (nonexistent)
  if(r != 0)
    fail("Symlinking to nonexistent file should succeed\n");

  r = symlink("/testsymlink/2", "/testsymlink/1");   //  1 -> (2)
  if(r) fail("Failed to link 1->2");
  r = symlink("/testsymlink/3", "/testsymlink/2");   //  2 -> (3)
  if(r) fail("Failed to link 2->3");
  r = symlink("/testsymlink/4", "/testsymlink/3");   //  3 -> (4)
  if(r) fail("Failed to link 3->4");

  close(fd1);
  close(fd2);

  fd1 = open("/testsymlink/4", O_CREATE | O_RDWR);
  if(fd1<0) fail("Failed to create 4\n");
  fd2 = open("/testsymlink/1", O_RDWR);
  if(fd2<0) fail("Failed to open 1\n");

  c = '#';
  r = write(fd2, &c, 1);
  if(r!=1) fail("Failed to write to 1\n");
  r = read(fd1, &c2, 1);
  if(r!=1) fail("Failed to read from 4\n");
  if(c!=c2)
    fail("Value read from 4 differed from value written to 1\n");

  printf("test symlinks: ok\n");
done:
  close(fd1);
  close(fd2);
}

#if 1
// 个人理解，只要保证多线程过程中，每个单独的函数执行完整即可，即使失败，能正常退出和清理，也ok
static void
concur(void)
{
  int pid, i;
  int fd;
  struct stat st;
  int nchild = 2;

  printf("Start: test concurrent symlinks\n");
    
  fd = open("/testsymlink/z", O_CREATE | O_RDWR);
  if(fd < 0) {
    printf("FAILED: open failed");
    exit(1);
  }
  close(fd);

  for(int j = 0; j < nchild; j++) {
    pid = fork();
    if(pid < 0){
      printf("FAILED: fork failed\n");
      exit(1);
    }
    if(pid == 0) {
      int m = 0;
      unsigned int x = (pid ? 1 : 97);
      for(i = 0; i < 100; i++){
        x = x * 1103515245 + 12345;
        // 创建软连接 或者释放软连接
        if((x % 3) == 0) {
          symlink("/testsymlink/z", "/testsymlink/y");  // y -> z
          // 可以返回 -1， 只要过程中能正常即可
          if (stat_slink("/testsymlink/y", &st) == 0) {
            m++;
            if(st.type != T_SYMLINK) {
              printf("FAILED: not a symbolic link\n", st.type);
              exit(1);
            }
          }
        } else {
          // 直接释放，无论是否重复释放
          unlink("/testsymlink/y");
        }
      }
      exit(0);
    }
  }

  int r;
  for(int j = 0; j < nchild; j++) {
    wait(&r);
    if(r != 0) {
      printf("test concurrent symlinks: failed\n");
      exit(1);
    }
  }
  printf("test concurrent symlinks: ok\n");
}

#endif
