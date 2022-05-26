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
static struct tx_desc tx_ring[TX_RING_SIZE] __attribute__((aligned(16)));
static struct mbuf *tx_mbufs[TX_RING_SIZE];

#define RX_RING_SIZE 16
static struct rx_desc rx_ring[RX_RING_SIZE] __attribute__((aligned(16)));
static struct mbuf *rx_mbufs[RX_RING_SIZE];

// remember where the e1000's registers live.
static volatile uint32 *regs;

struct spinlock e1000_lock;
struct spinlock e1000_lock_transmit;
struct spinlock e1000_lock_recv;

// called by pci_init().
// xregs is the memory address at which the
// e1000's registers are mapped.
void
e1000_init(uint32 *xregs)
{
  int i;

  initlock(&e1000_lock, "e1000");
  initlock(&e1000_lock_transmit, "e1000_lock_transmit");
  initlock(&e1000_lock_recv, "e1000_lock_recv");

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
  regs[E1000_TDBAL] = (uint64) tx_ring;
  if(sizeof(tx_ring) % 128 != 0)
    panic("e1000");
  regs[E1000_TDLEN] = sizeof(tx_ring);
  regs[E1000_TDH] = regs[E1000_TDT] = 0;
  
  // [E1000 14.4] Receive initialization
  memset(rx_ring, 0, sizeof(rx_ring));
  for (i = 0; i < RX_RING_SIZE; i++) {
    rx_mbufs[i] = mbufalloc(0);
    if (!rx_mbufs[i])
      panic("e1000");
    rx_ring[i].addr = (uint64) rx_mbufs[i]->head;
  }
  regs[E1000_RDBAL] = (uint64) rx_ring;
  if(sizeof(rx_ring) % 128 != 0)
    panic("e1000");
  regs[E1000_RDH] = 0;
  regs[E1000_RDT] = RX_RING_SIZE - 1;
  regs[E1000_RDLEN] = sizeof(rx_ring);

  // filter by qemu's MAC address, 52:54:00:12:34:56
  regs[E1000_RA] = 0x12005452;
  regs[E1000_RA+1] = 0x5634 | (1<<31);
  // multicast table
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

int
e1000_transmit(struct mbuf *m)
{
  acquire(&e1000_lock_transmit);
  int next_index = regs[E1000_TDT];
  struct tx_desc *found_desc = &tx_ring[next_index];
  if (!(found_desc->status & E1000_TXD_STAT_DD)) {  // not transmit complete..
    // printf("tttttt\n");
    release(&e1000_lock_transmit);
    return -1;
  } else {
    if (tx_mbufs[next_index])
      mbuffree(tx_mbufs[next_index]);
  }

  tx_mbufs[next_index] = m;
  found_desc->addr = (uint64)m->head;
  found_desc->cmd = E1000_TXD_CMD_EOP | E1000_TXD_CMD_RS;
  found_desc->status = 0;
  found_desc->length = m->len;
  
  // barrier, instruction not prefetch after instruction
  __sync_synchronize();

  regs[E1000_TDT] = (next_index+1) % TX_RING_SIZE;
  release(&e1000_lock_transmit);
  // printf("got transmit\n");
  return 0;
}

static void
e1000_recv(void)
{
  // why while true. during handing interrupt, the incoming interrupt is disabled.
  while(1){
  // acquire(&e1000_lock_recv);
  int next_index = (regs[E1000_RDT]+1) % RX_RING_SIZE;
  struct rx_desc *found_desc = &rx_ring[next_index];
  if (!(found_desc->status & E1000_RXD_STAT_DD)) {  // receiver not handle complete..
    // release(&e1000_lock_recv);
    return;
  } 
  struct mbuf *found_mbuf = rx_mbufs[next_index];  
  found_mbuf->len = found_desc->length;
  net_rx(found_mbuf);

  struct mbuf *new_mbuf = mbufalloc(MBUF_DEFAULT_HEADROOM);
  if (!new_mbuf) {
    // release(&e1000_lock_recv);
    return;
  }
    
  rx_mbufs[next_index] = new_mbuf;
  found_desc->addr = (uint64)new_mbuf->head;
  found_desc->status = 0;

  regs[E1000_RDT] = next_index;
  // release(&e1000_lock_recv);

  // printf("got recv\n");
  }
}

void
e1000_intr(void)
{
  // tell the e1000 we've seen this interrupt;
  // without this the e1000 won't raise any
  // further interrupts.
  regs[E1000_ICR] = 0xffffffff;

  e1000_recv();
}