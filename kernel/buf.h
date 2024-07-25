struct buf {
  int valid;   // has data been read from disk?
  int disk;    // does disk "own" buf?
  uint dev;
  uint blockno;
  struct sleeplock lock;
  uint refcnt;
#if 0
  struct buf *prev; // LRU cache list
  struct buf* next;
#else
  struct buf *next;   // backet 的链表
  uint64 hash_num;
  int bucket_index;   // 标记当前buf所在bucket的固定索引
  int head_index;     // 如果被编入list，则记录list头部bucket的索引
#endif
  uchar data[BSIZE];
};

