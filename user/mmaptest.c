#include "kernel/param.h"
#include "kernel/fcntl.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/riscv.h"
#include "kernel/fs.h"
#include "user/user.h"

void mmap_test();
void fork_test();
char buf[BSIZE];  // 1024

#define MAP_FAILED ((char *) -1)

/**

1. Fill in the page table lazily, in response to page faults. That is, mmap should not allocate physical
memory or read the file. Instead, do that in page fault handling code in (or called by) usertrap , as in
the copy-on-write lab. The reason to be lazy is to ensure that mmap of a large file is fast, and that mmap
of a file larger than physical memory is possible.

延时申请，好处：1. 加速调用  2. mmap 一个大于物理内存的文件是可能的

2. Keep track of what mmap has mapped for each process. Define a structure corresponding to the VMA
(virtual memory area) described in the "virtual memory for applications" lecture. This should record
the address, length, permissions, file, etc. for a virtual memory range created by mmap . Since the xv6
kernel doesn't have a memory allocator in the kernel, it's OK to declare a fixed-size array of VMAs
and allocate from that array as needed. A size of 16 should be sufficient（足够的）.


3. Implement mmap : find an unused region in the process's address space in which to map the file, and add
a VMA to the process's table of mapped regions. The VMA should contain a pointer to a struct file
for the file being mapped; mmap should increase the file's reference count so that the structure doesn't
disappear when the file is closed (hint: see filedup ). Run mmaptest : the first mmap should succeed, but
the first access to the mmap-ed memory will cause a page fault and kill mmaptest .

file's reference count ++

4. Add code to cause a page-fault in a mmap-ed region to allocate a page of physical memory, read 4096
bytes of the relevant file into that page, and map it into the user address space. Read the file with
readi , which takes an offset argument at which to read in the file (but you will have to lock/unlock the
inode passed to readi ). Don't forget to set the permissions correctly on the page. Run mmaptest ; it
should get to the first munmap .

file data  ->  readi -> copy into memory page

5. Implement munmap : find the VMA for the address range and unmap the specified pages (hint: use
uvmunmap ). If munmap removes all pages of a previous mmap , it should decrement the reference count of
the corresponding struct file . If an unmapped page has been modified and the file is mapped
MAP_SHARED , write the page back to the file. Look at filewrite for inspiration(灵感).

munmap ->  write ->  file
file's reference count --

6. Ideally your implementation would only write back MAP_SHARED pages that the program actually
modified. The dirty bit ( D ) in the RISC-V PTE indicates whether a page has been written. However,
mmaptest does not check that non-dirty pages are not written back; thus you can get away with writing
pages back without looking at D bits.[但是，mmaptest不会检查非脏页是否被写回；因此，您可以在不查看D位的情况下写回页面。]


7. Modify exit to unmap the process's mapped regions as if munmap had been called. Run mmaptest ;
mmap_test should pass, but probably not fork_test .


8. Modify fork to ensure that the child has the same mapped regions as the parent. Don't forget to
increment the reference count for a VMA's struct file . In the page fault handler of the child, it is
OK to allocate a new physical page instead of sharing a page with the parent. The latter would be
cooler, but it would require more implementation work. Run mmaptest ; it should pass both mmap_test
and fork_test .

implement the allocate new physical page in child.

*/


int
main(int argc, char *argv[])
{
  mmap_test();
  fork_test();
  printf("mmaptest: all tests succeeded\n");
  exit(0);
}

char *testname = "???";

void
err(char *why)
{
  printf("mmaptest: %s failed: %s, pid=%d\n", testname, why, getpid());
  exit(1);
}

//
// check the content of the two mapped pages.
//
void
_v1(char *p)
{
  int i;
  for (i = 0; i < PGSIZE*2; i++) {
    if (i < PGSIZE + (PGSIZE/2)) {
      if (p[i] != 'A') {
        printf("mismatch at %d, wanted 'A', got 0x%x\n", i, p[i]);
        err("v1 mismatch (1)");
      }
    } else {
      if (p[i] != 0) {
        printf("mismatch at %d, wanted zero, got 0x%x\n", i, p[i]);
        err("v1 mismatch (2)");
      }
    }
  }
}

//
// create a file to be mapped, containing
// 1.5 pages of 'A' and half a page of zeros.
//
void
makefile(const char *f)
{
  int i;
  int n = PGSIZE/BSIZE;

  unlink(f);
  int fd = open(f, O_WRONLY | O_CREATE);
  if (fd == -1)
    err("open");
  memset(buf, 'A', BSIZE);
  // write 1.5 page
  for (i = 0; i < n + n/2; i++) {
    if (write(fd, buf, BSIZE) != BSIZE)
      err("write 0 makefile");
  }
  if (close(fd) == -1)
    err("close");
}

void
mmap_test(void)
{
  int fd;
  int i;
  const char * const f = "mmap.dur";
  printf("mmap_test starting\n");
  testname = "mmap_test";

  //
  // create a file with known content, map it into memory, check that
  // the mapped memory has the same bytes as originally written to the
  // file.
  //
  makefile(f);
  if ((fd = open(f, O_RDONLY)) == -1)
    err("open (1)");

  printf("test mmap f\n");
  //
  // this call to mmap() asks the kernel to map the content
  // of open file fd into the address space. the first
  // 0 argument indicates that the kernel should choose the
  // virtual address. the second argument indicates how many
  // bytes to map. the third argument indicates that the
  // mapped memory should be read-only. the fourth argument
  // indicates that, if the process modifies the mapped memory,
  // that the modifications should not be written back to
  // the file nor shared with other processes mapping the
  // same file (of course in this case updates are prohibited
  // due to PROT_READ). the fifth argument is the file descriptor
  // of the file to be mapped. the last argument is the starting
  // offset in the file.
  //
  char *p = mmap(0, PGSIZE*2, PROT_READ, MAP_PRIVATE, fd, 0);
  if (p == MAP_FAILED)
    err("mmap (1)");
  _v1(p);
  if (munmap(p, PGSIZE*2) == -1)
    err("munmap (1)");

  printf("test mmap f: OK\n");

  printf("test mmap private\n");
  // should be able to map file opened read-only with private writable
  // mapping
  p = mmap(0, PGSIZE*2, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
  if (p == MAP_FAILED)
    err("mmap (2)");
  if (close(fd) == -1)
    err("close (1)");
  _v1(p);
  for (i = 0; i < PGSIZE*2; i++)
    p[i] = 'Z';
  if (munmap(p, PGSIZE*2) == -1)
    err("munmap (2)");

  printf("test mmap private: OK\n");

  printf("test mmap read-only\n");

  // check that mmap doesn't allow read/write mapping of a
  // file opened read-only.
  if ((fd = open(f, O_RDONLY)) == -1)
    err("open (2)");
  p = mmap(0, PGSIZE*3, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (p != MAP_FAILED)
    err("mmap (3)");
  if (close(fd) == -1)
    err("close (2)");

  printf("test mmap read-only: OK\n");

  printf("test mmap read/write\n");

  // check that mmap does allow read/write mapping of a
  // file opened read/write.
  if ((fd = open(f, O_RDWR)) == -1)
    err("open (3)");
  p = mmap(0, PGSIZE*3, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (p == MAP_FAILED)
    err("mmap (4)");
  if (close(fd) == -1)
    err("close (3)");

  // check that the mapping still works after close(fd).
  _v1(p);

  // write the mapped memory.
  for (i = 0; i < PGSIZE*2; i++)
    p[i] = 'Z';

  // unmap just the first two of three pages of mapped memory.
  if (munmap(p, PGSIZE*2) == -1)
    err("munmap (3)");

  printf("test mmap read/write: OK\n");

  printf("test mmap dirty\n");

  // check that the writes to the mapped memory were
  // written to the file.
  if ((fd = open(f, O_RDWR)) == -1)
    err("open (4)");
  for (i = 0; i < PGSIZE + (PGSIZE/2); i++){
    char b;
    if (read(fd, &b, 1) != 1)
      err("read (1)");
    if (b != 'Z')
      err("file does not contain modifications");
  }
  if (close(fd) == -1)
    err("close (4)");

  printf("test mmap dirty: OK\n");

  printf("test not-mapped unmap\n");

  // unmap the rest of the mapped memory.
  if (munmap(p+PGSIZE*2, PGSIZE) == -1)
    err("munmap (4)");

  printf("test not-mapped unmap: OK\n");

  printf("test mmap two files\n");

  //
  // mmap two files at the same time.
  //
  int fd1;
  if((fd1 = open("mmap1", O_RDWR|O_CREATE)) < 0)
    err("open (5)");
  if(write(fd1, "12345", 5) != 5)
    err("write (1)");
  char *p1 = mmap(0, PGSIZE, PROT_READ, MAP_PRIVATE, fd1, 0);
  if(p1 == MAP_FAILED)
    err("mmap (5)");
  if (close(fd1) == -1)
    err("close (5)");
  // 是的，可以直接删除，在 PROT_READ的时候
  // 即使为 PROT_WRITE 下，也可以直接删除，mmap的过程不会受影响，只不过 MAP_SHARED下，munmap不会将数据写会文件
  // 但munmap也不会报错
  if (unlink("mmap1") == -1)
    err("unlink (1)");

  int fd2;
  if((fd2 = open("mmap2", O_RDWR|O_CREATE)) < 0)
    err("open (6)");
  if(write(fd2, "67890", 5) != 5)
    err("write (2)");
  char *p2 = mmap(0, PGSIZE, PROT_READ, MAP_PRIVATE, fd2, 0);
  if(p2 == MAP_FAILED)
    err("mmap (6)");
  if (close(fd2) == -1)
    err("close (6)");
  if (unlink("mmap2") == -1)  // 直接删除
    err("unlink (2)");

  if(memcmp(p1, "12345", 5) != 0)
    err("mmap1 mismatch");
  if(memcmp(p2, "67890", 5) != 0)
    err("mmap2 mismatch");

  if (munmap(p1, PGSIZE) == -1)
    err("munmap (5)");
  if(memcmp(p2, "67890", 5) != 0)
    err("mmap2 mismatch (2)");
  if (munmap(p2, PGSIZE) == -1)
    err("munmap (6)");

  printf("test mmap two files: OK\n");

  printf("mmap_test: ALL OK\n");
}

//
// mmap a file, then fork.
// check that the child sees the mapped file.
//
void
fork_test(void)
{
  int fd;
  int pid;
  const char * const f = "mmap.dur";

  printf("fork_test starting\n");
  testname = "fork_test";

  // mmap the file twice.
  makefile(f);
  if ((fd = open(f, O_RDONLY)) == -1)
    err("open (7)");
  if (unlink(f) == -1)
    err("unlink (3)");
  char *p1 = mmap(0, PGSIZE*2, PROT_READ, MAP_SHARED, fd, 0);
  if (p1 == MAP_FAILED)
    err("mmap (7)");
  char *p2 = mmap(0, PGSIZE*2, PROT_READ, MAP_SHARED, fd, 0);
  if (p2 == MAP_FAILED)
    err("mmap (8)");

  // read just 2nd page.
  if(*(p1+PGSIZE) != 'A')
    err("fork mismatch (1)");

  if((pid = fork()) < 0)
    err("fork");
  if (pid == 0) {
    _v1(p1);
    if (munmap(p1, PGSIZE) == -1) // just the first page
      err("munmap (7)");
    exit(0); // tell the parent that the mapping looks OK.
  }

  int status = -1;
  wait(&status);

  if(status != 0){
    printf("fork_test failed\n");
    exit(1);
  }

  // check that the parent's mappings are still there.
  _v1(p1);
  _v1(p2);

  printf("fork_test OK\n");
}


/*
Optional challenges

// choose 1 and 5 to fix

1. If two processes have the same file mmap-ed (as in fork_test ), share their physical pages. You will
need reference counts on physical pages.

2. Your solution probably allocates a new physical page for each page read from the mmap-ed file, even
though the data is also in kernel memory in the buffer cache. Modify your implementation to use that
physical memory, instead of allocating a new page. This requires that file blocks be the same size as
pages (set BSIZE to 4096). You will need to pin mmap-ed blocks into the buffer cache. You will need
worry about reference counts.

3. Remove redundancy between your implementation for lazy allocation and your implementation of
mmap-ed files. (Hint: create a VMA for the lazy allocation area.)

4. Modify exec to use a VMA for different sections of the binary so that you get on-demand-paged
executables. This will make starting programs faster, because exec will not have to read any data from
the file system.

5. Implement page-out and page-in: have the kernel move some parts of processes to disk when physical
memory is low.[当物理内存不足时，让内核将进程的某些部分移动到磁盘] Then, page in the paged-out memory when the
process references it.
*/
