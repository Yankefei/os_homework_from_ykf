#define NPROC        64  // maximum number of processes
#define NCPU         SMP_SIZE  // maximum number of CPUs
#define NOFILE       16  // open files per process
#define NFILE       100  // open files per system
#define NINODE       50  // maximum number of active i-nodes
#define NDEV         10  // maximum major device number
#define ROOTDEV       1  // device number of file system root disk
#define MAXARG       32  // max exec arguments

// 注释下面每个枚举值的定义
#define MAXOPBLOCKS  10  // max # of blocks any FS op writes
#define LOGSIZE      (MAXOPBLOCKS*3)  // max data blocks in on-disk log
#if 0
#define NBUF         (MAXOPBLOCKS*3)  // size of disk block cache
#else
// 增加缓存的数量到65，bucket列为13， 每列是5个元素的数组
#define NBUF         65
#endif
// #define NBUF  65
#define FSSIZE       2000  // size of file system in blocks
#define MAXPATH      128   // maximum file path name
