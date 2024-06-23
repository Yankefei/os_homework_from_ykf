#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <pthread.h>
#include <sys/time.h>

#define NBUCKET 5
#define NKEYS 100000

struct entry {
  int key;
  int value;
  struct entry *next;
};

struct bucketInfo {
  struct entry* entry;
  pthread_mutex_t lock;
};

// struct entry *table[NBUCKET]; // 指针数组
struct bucketInfo table[NBUCKET];

int keys[NKEYS];
int nthread = 1;


double
now()
{
 struct timeval tv;
 gettimeofday(&tv, 0);
 return tv.tv_sec + tv.tv_usec / 1000000.0;
}

static void 
insert(int key, int value, struct entry **p, struct entry *n)
{
  struct entry *e = malloc(sizeof(struct entry));
  e->key = key;
  e->value = value;
  // 先设置next, 然后才替换 *p, 连表前插法
  e->next = n;
  *p = e; // 重复插入导致旧值丢失？？
}

// test
void printfTable() {
  for (int i = 0; i < NBUCKET; i++) {

    printf("table[%d]:\n", i);

    struct entry *e = 0;
    int i = 0;
    for (e = table[i].entry; e != 0; e = e->next, i++) {}
    printf("next, size %d\n", i);
  }
}

// key 重复， 有可能， 因为是从keys数组中取值的
static 
void put(int key, int value)
{
  int i = key % NBUCKET;  // 0 - 4

  // is the key already present?
  struct entry *e = 0;
  for (e = table[i].entry; e != 0; e = e->next) {
    if (e->key == key) {
      printf("present: key: %d, val: %d\n", key, value);
      break;
    }
  }
  if(e){
    // update the existing key.
    e->value = value;
  } else {
    // the new is new.
    pthread_mutex_lock(&table[i].lock);
    insert(key, value, &(table[i].entry), table[i].entry);
    pthread_mutex_unlock(&table[i].lock);
  }
}

static struct entry*
get(int key)
{
  int i = key % NBUCKET;


  struct entry *e = 0;
  for (e = table[i].entry; e != 0; e = e->next) {
    if (e->key == key) break;
  }

  return e;
}

// input xa, thread_id
static void *
put_thread(void *xa)
{
  int n = (int) (long) xa; // thread number  2
  int b = NKEYS/nthread;   // nthread all thread_num  1 0-100000

  // 每个线程均匀put 划分好的key区间, value 为 thread_id
  for (int i = 0; i < b; i++) {
    put(keys[b*n + i], n);
  }

  return NULL;
}

// 每个线程都会get一次数据
static void *
get_thread(void *xa)
{
  int n = (int) (long) xa; // thread number
  int missing = 0;

  for (int i = 0; i < NKEYS; i++) {
    struct entry *e = get(keys[i]);
    if (e == 0) missing++;
  }
  printf("%d: %d keys missing\n", n, missing);
  return NULL;
}

int
main(int argc, char *argv[])
{
  pthread_t *tha;
  void *value;
  double t1, t0;


  if (argc < 2) {
    fprintf(stderr, "Usage: %s nthreads\n", argv[0]);
    exit(-1);
  }
  nthread = atoi(argv[1]); // 1, 没有missing的问题，但是2，就会出现，线程安全问题要处理
  tha = malloc(sizeof(pthread_t) * nthread);
  srandom(0);
  assert(NKEYS % nthread == 0);
  for (int i = 0; i < NKEYS; i++) {
    keys[i] = random(); // 产生随机
  }

  for (int i = 0; i < NBUCKET; i++) {
    table[i].entry = NULL;
    pthread_mutex_init(&table[i].lock, NULL);
  }

  //
  // first the puts
  //
  t0 = now();
  for(int i = 0; i < nthread; i++) {
    assert(pthread_create(&tha[i], NULL, put_thread, (void *) (long) i) == 0);
  }
  for(int i = 0; i < nthread; i++) {
    assert(pthread_join(tha[i], &value) == 0);
  }
  t1 = now();

  printf("%d puts, %.3f seconds, %.0f puts/second\n",
         NKEYS, t1 - t0, NKEYS / (t1 - t0));
  // printf("key[i], repeat, num: %d\n", g_key_repeat);

  //
  // now the gets
  //
  t0 = now();
  for(int i = 0; i < nthread; i++) {
    assert(pthread_create(&tha[i], NULL, get_thread, (void *) (long) i) == 0);
  }
  for(int i = 0; i < nthread; i++) {
    assert(pthread_join(tha[i], &value) == 0);
  }
  t1 = now();

  printf("%d gets, %.3f seconds, %.0f gets/second\n",
         NKEYS*nthread, t1 - t0, (NKEYS*nthread) / (t1 - t0));

  printfTable();
}
