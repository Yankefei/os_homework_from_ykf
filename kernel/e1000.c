#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "e1000_dev.h"
#include "net.h"

#define TX_RING_SIZE 16
// 单个结构体 128bytes
static struct tx_desc tx_ring[TX_RING_SIZE] __attribute__((aligned(16)));
static struct mbuf *tx_mbufs[TX_RING_SIZE];

#define RX_RING_SIZE 16
// 单个结构体 128bytes
static struct rx_desc rx_ring[RX_RING_SIZE] __attribute__((aligned(16)));
static struct mbuf *rx_mbufs[RX_RING_SIZE];

// remember where the e1000's registers live. 长度 0x20000
static volatile uint32 *regs;

struct spinlock e1000_lock;

// called by pci_init().
// xregs is the memory address at which the
// e1000's registers are mapped.
void
e1000_init(uint32 *xregs)
{
  int i;

  initlock(&e1000_lock, "e1000");

  regs = xregs;

  // Reset the device
  regs[E1000_IMS] = 0; // disable interrupts
  regs[E1000_CTL] |= E1000_CTL_RST;
  regs[E1000_IMS] = 0; // redisable interrupts
  __sync_synchronize();

  // [E1000 14.5] Transmit initialization
  memset(tx_ring, 0, sizeof(tx_ring));
  for (i = 0; i < TX_RING_SIZE; i++) {
    tx_ring[i].status = E1000_TXD_STAT_DD;
    tx_mbufs[i] = 0;
  }
  // 基本上 regs[E1000_TDBAH] 也会被赋值
  regs[E1000_TDBAL] = (uint64) tx_ring;
  if(sizeof(tx_ring) % 128 != 0)
    panic("e1000");
  regs[E1000_TDLEN] = sizeof(tx_ring); // 256
  regs[E1000_TDH] = regs[E1000_TDT] = 0; // 空链
  
  // [E1000 14.4] Receive initialization
  memset(rx_ring, 0, sizeof(rx_ring));
  for (i = 0; i < RX_RING_SIZE; i++) {
    rx_mbufs[i] = mbufalloc(0);
    if (!rx_mbufs[i])
      panic("e1000");
    rx_ring[i].addr = (uint64) rx_mbufs[i]->head; // rx_ring 的addr 仅保存指针
  }
  regs[E1000_RDBAL] = (uint64) rx_ring; // base address low
  if(sizeof(rx_ring) % 128 != 0)
    panic("e1000");
  regs[E1000_RDH] = 0;                  // head  0
  regs[E1000_RDT] = RX_RING_SIZE - 1;   // tail  15
  regs[E1000_RDLEN] = sizeof(rx_ring);  // length  256

  // filter by qemu's MAC address, 52:54:00:12:34:56
  regs[E1000_RA] = 0x12005452;
  regs[E1000_RA+1] = 0x5634 | (1<<31);
  // multicast table
  // 128
  for (int i = 0; i < 4096/32; i++)
    regs[E1000_MTA + i] = 0;

  // transmitter control bits.
  regs[E1000_TCTL] = E1000_TCTL_EN |  // enable
    E1000_TCTL_PSP |                  // pad short packets
    (0x10 << E1000_TCTL_CT_SHIFT) |   // collision stuff
    (0x40 << E1000_TCTL_COLD_SHIFT);
  regs[E1000_TIPG] = 10 | (8<<10) | (6<<20); // inter-pkt gap

  // receiver control bits.
  regs[E1000_RCTL] = E1000_RCTL_EN | // enable receiver
    E1000_RCTL_BAM |                 // enable broadcast
    E1000_RCTL_SZ_2048 |             // 2048-byte rx buffers
    E1000_RCTL_SECRC;                // strip CRC
  
  // ask e1000 for receive interrupts.
  regs[E1000_RDTR] = 0; // interrupt after every received packet (no timer)
  regs[E1000_RADV] = 0; // interrupt after every packet (no timer)
  regs[E1000_IMS] = (1 << 7); // RXDW -- Receiver Descriptor Write Back
}

void printSendRingInfo() {
  int tail_index = regs[E1000_TDT];
  int tail_index_m = (tail_index) % TX_RING_SIZE;

  int head_index = regs[E1000_TDH];
  int head_index_m = (head_index) % TX_RING_SIZE;

  // 数据顺序，tail为旧， head 为新
  printf("send: tail %d  %d   head %d  %d\n",
    tail_index, tail_index_m, head_index, head_index_m);
  for (int j = 0; j < TX_RING_SIZE; j++) {
    struct tx_desc* desc = &tx_ring[j];
    printf("tx_ring show status, index: %d, status: %p, mbuf: %p\n",
      (j % TX_RING_SIZE), desc->status, tx_mbufs[j]);
  }
}


// 异步接口
int
e1000_transmit(struct mbuf *m)
{
  //
  // Your code here.
  //
  // the mbuf contains an ethernet frame; program it into
  // the TX descriptor ring so that the e1000 sends it. Stash
  // a pointer so that it can be freed after sending.
  //

  // E1000_TXD_STAT_DD. E1000 has finished transmitting the packet

  // printf("e1000_transmit\n");

  // push data
  acquire(&e1000_lock);

  // printSendRingInfo();

  int tail_index = regs[E1000_TDT];
  int tail_index_m = tail_index % TX_RING_SIZE;
  if (tx_ring[tail_index_m].status != E1000_TXD_STAT_DD) {
    printf("tx_send_buf is full, tail_index: %d\n", tail_index);
    release(&e1000_lock);
    return -1;
  }

  // send的时候，head_index 是软件维护的，tail_index则是硬件在处理
  int head_index = regs[E1000_TDH];
  int head_index_m = head_index % TX_RING_SIZE;

  // 无需循环处理，因为这个地方是临界区，不会有多线程的访问，每次只处理上一个mbuf即可
  int last_tail_index = tail_index == 0  ? (TX_RING_SIZE - 1) : (tail_index - 1);
  struct mbuf* ptr = tx_mbufs[last_tail_index % TX_RING_SIZE];
  if (ptr) {
    mbuffree(ptr);
    tx_mbufs[last_tail_index % TX_RING_SIZE] = 0;
    // printf("send... delete buf\n");
  }

  tx_ring[head_index_m].addr = (uint64)m->head;
  tx_ring[head_index_m].length = m->len;
  tx_ring[head_index_m].cmd = E1000_TXD_CMD_RS | E1000_TXD_CMD_EOP;
  tx_ring[head_index_m].css = 0;
  tx_mbufs[head_index_m] = m;

  // update ring
  regs[E1000_TDT] = (head_index + 1) % TX_RING_SIZE;

  release(&e1000_lock);

  return 0;
}

void printRecvRingInfo() {
  int tail_index = regs[E1000_RDT];
  int tail_index_m = (tail_index) % RX_RING_SIZE;

  int head_index = regs[E1000_RDH];
  int head_index_m = (head_index) % RX_RING_SIZE;

  // int recv_len = regs[E1000_RDLEN];

  // recv: tail 15  0   head 10  11
  // 数据顺序，tail为旧， head 为新
  printf("recv: tail %d  %d   head %d  %d\n",
    tail_index, tail_index_m, head_index, head_index_m);
  /**
    rx_ring show status, index: 0, status: 0x0000000000000007
    rx_ring show status, index: 1, status: 0x0000000000000007
    rx_ring show status, index: 2, status: 0x0000000000000007
    rx_ring show status, index: 3, status: 0x0000000000000007
    rx_ring show status, index: 4, status: 0x0000000000000007
    rx_ring show status, index: 5, status: 0x0000000000000007
    rx_ring show status, index: 6, status: 0x0000000000000007
    rx_ring show status, index: 7, status: 0x0000000000000007
    rx_ring show status, index: 8, status: 0x0000000000000007
    rx_ring show status, index: 9, status: 0x0000000000000007
    rx_ring show status, index: 10, status: 0x0000000000000000  head
    rx_ring show status, index: 11, status: 0x0000000000000000
    rx_ring show status, index: 12, status: 0x0000000000000000
    rx_ring show status, index: 13, status: 0x0000000000000000
    rx_ring show status, index: 14, status: 0x0000000000000000
    rx_ring show status, index: 15, status: 0x0000000000000000   tail
  */
  for (int j = 0; j < RX_RING_SIZE; j++) {
    struct rx_desc* desc = &rx_ring[j % RX_RING_SIZE];
    printf("rx_ring show status, index: %d, status: %p", (j % RX_RING_SIZE), desc->status);

    struct mbuf * buf = mbufalloc(0);
    mbufput(buf, desc->length);
    memmove(buf->head, (void*)desc->addr, desc->length);
    net_rx_print_type(buf);
    mbuffree(buf);
    printf("\n");
  }
}

/**
 * 当网卡在发送arp reply后，如果收到了来自对端的IP报文，应该会立即插入 rx_ring的队列中
 * 对这部分数据做提升优先级的处理，所以这里面的head_index 应该会变动才对
*/
static void
e1000_recv(void)
{
  //
  // Your code here.
  //
  // Check for packets that have arrived from the e1000
  // Create and deliver an mbuf for each packet (using net_rx()).
  //

  // You will then need
  // to allocate a new mbuf and place it into the descriptor, so that when
  // the E1000 reaches that point in the RX
  // ring again it finds a fresh buffer into which to DMA a new packet.

#if 0  // v1. pass first second test

  int tail_index = regs[E1000_RDT];
  // int head_index = regs[E1000_RDH];

  struct rx_desc* desc = &rx_ring[(tail_index + 1) % RX_RING_SIZE];

  if (!(desc->status & E1000_RXD_STAT_DD)) {
    printf("rx_ring status is invalid, index: %d, status: %p\n", (tail_index % RX_RING_SIZE), desc->status);
    return;
  }

  // will free in net_rx
  struct mbuf * buf = mbufalloc(0);
  mbufput(buf, desc->length);
  memmove(buf->head, (void*)desc->addr, desc->length);
  desc->status = 0;
  net_rx(buf);
  regs[E1000_RDT] = tail_index + 1;

#endif


#if 0 // v2  can recv data
  int tail_index = regs[E1000_RDT];
  int head_index = regs[E1000_RDH];

  // 环形队列，从tail 遍历到head
  // 先判断数据是否存在
  int head_index_r = head_index;
  if (head_index < tail_index) {
    head_index_r = head_index + RX_RING_SIZE;
  }
  for (int i = tail_index + 1; i < head_index_r; i++) {

    printRecvRingInfo();

    struct rx_desc* desc = &rx_ring[i % RX_RING_SIZE];
    if (!(desc->status & E1000_RXD_STAT_DD)) {
      printf("rx_ring status is invalid, index: %d, status: %p\n", (i % RX_RING_SIZE), desc->status);
      return;
    }

    // will free in net_rx
    struct mbuf * buf = mbufalloc(0);
    mbufput(buf, desc->length);
    memmove(buf->head, (void*)desc->addr, desc->length);
    desc->status = 0;
    printf("net_rx begin, new head_idex: %d, new tail_index: %d, will set i: %d to RHT\n", regs[E1000_RDH], regs[E1000_RDT], i);
    net_rx(buf);

    printf("net_rx after, new head_idex: %d, new tail_index: %d, will set i: %d to RHT\n", regs[E1000_RDH], regs[E1000_RDT], i);
    // ?
    regs[E1000_RDT] = i;//  + 1;

  }
#endif


#if 1  // v3
  int tail_index = regs[E1000_RDT];
  int head_index = regs[E1000_RDH];

  // 环形队列，从tail 遍历到head
/*
  recv: tail 104  8   head 10  10
  rx_ring show status, index: 0, status: 0x0000000000000000  ip
  rx_ring show status, index: 1, status: 0x0000000000000000  ip
  rx_ring show status, index: 2, status: 0x0000000000000000  ip
  rx_ring show status, index: 3, status: 0x0000000000000000  ip
  rx_ring show status, index: 4, status: 0x0000000000000000  ip
  rx_ring show status, index: 5, status: 0x0000000000000000  ip
  rx_ring show status, index: 6, status: 0x0000000000000000  ip
  rx_ring show status, index: 7, status: 0x0000000000000000  ip
  rx_ring show status, index: 8, status: 0x0000000000000000  ip  // tail
  rx_ring show status, index: 9, status: 0x0000000000000007  ip  // data
  rx_ring show status, index: 10, status: 0x0000000000000000  ip // head
  rx_ring show status, index: 11, status: 0x0000000000000000  ip
  rx_ring show status, index: 12, status: 0x0000000000000000  ip
  rx_ring show status, index: 13, status: 0x0000000000000000  ip
  rx_ring show status, index: 14, status: 0x0000000000000000  ip
  rx_ring show status, index: 15, status: 0x0000000000000000  ip
*/
  // 根据目前观察的日志判断， head_index 的值的范围是 0-15， tail_index的值却是递增的
  // 而且data的位置在tail 和  head 中间，head > data > tail
  // 所以处理的时候，需要先将 head_index的值，累加到 超过tail的一个范围
  // 处理的时候，也需要从tail + 1 的位置（data）处来开始处理
  // 从上面的例子中，也可以看到，当 tail + 1 == head, 数据就处理完了
  int head_index_r = head_index;
  while (head_index_r < tail_index) {
    head_index_r += RX_RING_SIZE;
    // printf("head: %d, tail: %d\n", head_index_r, tail_index);
  }

  // printf("e1000_recv... recv: tail %d  %d   head %d  %d, head_index_r: %d\n",
  //   tail_index, tail_index % RX_RING_SIZE, head_index, head_index % RX_RING_SIZE, head_index_r);

  for (int i = tail_index + 1; i < head_index_r; i++) {

    // printRecvRingInfo();

    struct rx_desc* desc = &rx_ring[i % RX_RING_SIZE];
    if (!(desc->status & E1000_RXD_STAT_DD)) {
      printf("rx_ring status is invalid, index: %d, status: %p\n", (i % RX_RING_SIZE), desc->status);
      return;
    }

    // will free in net_rx
    struct mbuf * buf = mbufalloc(0);
    mbufput(buf, desc->length);
    memmove(buf->head, (void*)desc->addr, desc->length);
    desc->status = 0;
    // printf("net_rx begin, new head_idex: %d, new tail_index: %d, will set i: %d to RHT\n", regs[E1000_RDH], regs[E1000_RDT], i);
    net_rx(buf);

    // printf("net_rx after, new head_idex: %d, new tail_index: %d, will set i: %d to RHT\n", regs[E1000_RDH], regs[E1000_RDT], i);

    // 每次都需要用最新处理的tail索引来更新 E1000_RDT， 保证Tail是持续递增的
    // head 的递增是硬件在处理，tail的递增就需要驱动软件了
    regs[E1000_RDT] = i;
  }
#endif

}

int once = 0;
void
e1000_intr(void)
{
  // printf("recv e1000_intr\n");

  // tell the e1000 we've seen this interrupt;
  // without this the e1000 won't raise any
  // further interrupts.
  regs[E1000_ICR] = 0xffffffff;

  e1000_recv();
}
