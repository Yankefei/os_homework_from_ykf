#define O_RDONLY  0x000
#define O_WRONLY  0x001
#define O_RDWR    0x002
#define O_CREATE  0x200
#define O_TRUNC   0x400

#ifdef LAB_MMAP
#define PROT_NONE       0x0

// 一句话解释： 映射的内存是只读的
#define PROT_READ       0x1

// 一句话解释： 映射的内存是非只读的
#define PROT_WRITE      0x2

//暂时未用
#define PROT_EXEC       0x4



// 一句话解释：如果进程修改了映射内存，可以写回文件 (如果是 PROT_READ， 则也是不需要更新的)
#define MAP_SHARED      0x01

// 一句话解释：如果进程修改了映射内存，不能写回内存
#define MAP_PRIVATE     0x02
#endif
