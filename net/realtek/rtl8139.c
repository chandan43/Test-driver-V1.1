#include <linux/module.h>
#include <linux/init.h>
#include <linux/compiler.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/if_vlan.h>

#define DRV_NAME	"realtek8139"
#define DRV_VERSION	".1"
#define RTL8139_DRIVER_NAME   DRV_NAME " Fast Ethernet driver " DRV_VERSION
//================>
/*The message level was not precisely defined past level 3, but were
 always implemented within +-1 of the specified level.  Drivers tended
 to shed the more verbose level messages as they matured.
    0  Minimal messages, only essential information on fatal errors.
    1  Standard messages, initialization status.  No run-time messages
    2  Special media selection messages, generally timer-driver.
    3  Interface starts and stops, including normal status messages
    4  Tx and Rx frame error messages, and abnormal driver operation
    5  Tx packet queue information, interrupt events.
    6  Status on each completed Tx packet and received Rx packets
    7  Initial contents of Tx and Rx packets
The set of message levels is named
  Old level   Name          Bit position
    0    NETIF_MSG_DRV		0x0001
    1    NETIF_MSG_PROBE	0x0002
    2    NETIF_MSG_LINK		0x0004
    2    NETIF_MSG_TIMER	0x0004
    3    NETIF_MSG_IFDOWN	0x0008
    3    NETIF_MSG_IFUP		0x0008
    4    NETIF_MSG_RX_ERR	0x0010
    4    NETIF_MSG_TX_ERR	0x0010
    5    NETIF_MSG_TX_QUEUED	0x0020
    5    NETIF_MSG_INTR		0x0020
    6    NETIF_MSG_TX_DONE	0x0040
    6    NETIF_MSG_RX_STATUS	0x0040
    7    NETIF_MSG_PKTDATA	0x0080
 */
/*The design of the debugging message interface was guided and constrained by backwards compatibility previous practice.*/
/* Default Message level */
#define RTL8139_DEF_MSG_ENABLE   (NETIF_MSG_DRV   |  NETIF_MSG_PROBE  | NETIF_MSG_LINK)


/* define to 1, 2 or 3 to enable copious debugging info */
#define RTL8139_DEBUG 0

/* define to 1 to disable lightweight runtime debugging checks */
#undef RTL8139_NDEBUG

/*
likely() and unlikely() are macros that Linux kernel developers use to give hints to the compiler and chipset. 
Modern CPUs have extensive branch-prediction heuristics that attempt to predict incoming commands in order to 
optimize speed. The likely() and unlikely() macros allow the developer to tell the CPU, through the compiler, 
that certain sections of code are likely, and thus should be predicted, or unlikely, so they shouldn't be predicted. 
They are defined in include/linux/compiler.h:
*/


#ifdef RTL8139_NDEBUG
#  define assert(expr) do {} while (0)
#else
#  define assert(expr) if (unlikely(!(expr))) { pr_err("Assertion failed! %s,%s,%s,line=%d\n",	#expr, __FILE__, __func__, __LINE__);}
#endif

/* A few user-configurable values. */
/* media options */
#define MAX_UNITS 8
static int media[MAX_UNITS] = {-1, -1, -1, -1, -1, -1, -1, -1};
static int full_duplex[MAX_UNITS] = {-1, -1, -1, -1, -1, -1, -1, -1};

/* Whether to use MMIO or PIO. Default to MMIO. */
#ifdef CONFIG_8139TOO_PIO
static bool use_io = true;
#else
static bool use_io = false;
#endif
/*IP multicast is a method of sending Internet Protocol (IP) datagrams to a group of interested receivers in a single transmission.*/

/*
 Maximum number of multicast addresses to filter (vs. Rx-all-multicast).
 The RTL chips use a 64 element hash table based on the Ethernet CRC.  It
 is efficient to update the hardware filter, but recalculating the table
 for a long filter list is painful
*/
static int multicast_filter_limit = 32;

/* bitmapped message enable number */
static int debug = -1;

/*
 * Receive ring size
 * Warning: 64K ring has hardware issues and may lock up.
 */
#if defined(CONFIG_SH_DREAMCAST)        /*Use declared coherent memory for dreamcast pci ethernet adapter*/
#define RX_BUF_IDX 0	/* 8K ring */
#else
#define RX_BUF_IDX	2	/* 32K ring */
#endif
#define RX_BUF_LEN	(8192 << RX_BUF_IDX)
#define RX_BUF_PAD	16   /* see 11th and 12th bit of RCR: 0x44 */
#define RX_BUF_WRAP_PAD 2048 /* spare padding to handle lack of packet wrap */

#if RX_BUF_LEN == 65536
#define RX_BUF_TOT_LEN	RX_BUF_LEN
#else
#define RX_BUF_TOT_LEN	(RX_BUF_LEN + RX_BUF_PAD + RX_BUF_WRAP_PAD)
#endif

/* Number of Tx descriptor registers.  The transmit path of RTL8139(A/B) use 4 descriptors .*/
#define NUM_TX_DESC	4

/* max supported ethernet frame size -- must be at least (dev->mtu+18+4).*/
#define MAX_ETH_FRAME_SIZE	1792

/* max supported payload size */
/* #define ETH_FCS_LEN     4           Octets in the FCS(frame check sequence)
   #define VLAN_ETH_HLEN   18           Total octets in header in if_vlan.h.       
*/
#define MAX_ETH_DATA_SIZE (MAX_ETH_FRAME_SIZE - VLAN_ETH_HLEN - ETH_FCS_LEN)    

/* Size of the Tx bounce buffers -- must be at least (dev->mtu+18+4). */
#define TX_BUF_SIZE	MAX_ETH_FRAME_SIZE
#define TX_BUF_TOT_LEN	(TX_BUF_SIZE * NUM_TX_DESC)

/* PCI Tuning Parameters Threshold is bytes transferred to chip before transmission starts. */
#define TX_FIFO_THRESH 256	/* In bytes, rounded down to 32 byte units. */
/* The following settings are log_2(bytes)-4:  0 == 16 bytes .. 6==1024, 7==end of packet. */

#define RX_FIFO_THRESH	7	/* Rx buffer level before first PCI xfer. Receive Configuration Register (Bit 15-13: RXFTH2, 1, 0) */
#define RX_DMA_BURST	7	/* Maximum PCI burst, '6' is 1024 (Receive Configuration Register ,BIT 10-8: MXDMA2, 1, 0)*/
#define TX_DMA_BURST	6	/* Maximum PCI burst, '6' is 1024 (Transmit Configuration Register , 10-8 MXDMA2, 1, 0)*/
#define TX_RETRY	8	/* 0-15.  retries = 16 + (TX_RETRY * 16) ( Transmit Configuration Register: Bit 7-4 ,TXRR)*/

/* Operational parameters that usually are not changed. */
/* Time in jiffies before concluding the transmitter is hung. */
#define TX_TIMEOUT  (6*HZ)
/*
	{ "100/10M Ethernet PCI Adapter",	HAS_CHIP_XCVR },
	{ "1000/100/10M Ethernet PCI Adapter",	HAS_MII_XCVR },
*/
enum {
	HAS_MII_XCVR = 0x010000,
	HAS_CHIP_XCVR = 0x020000,
	HAS_LNK_CHNG = 0x040000,
};
#define RTL_NUM_STATS 4		/* number of ETHTOOL_GSTATS u64's */
#define RTL_REGS_VER 1		/* version of reg. data in ETHTOOL_GREGS */
#define RTL_MIN_IO_SIZE 0x80
#define RTL8139B_IO_SIZE 256

#define RTL8129_CAPS	HAS_MII_XCVR
#define RTL8139_CAPS	(HAS_CHIP_XCVR|HAS_LNK_CHNG)


enum {
	RTL8139 = 0,
	RTL8129,
}board_t;
/* indexed by board_t, above */
static const struct {
	const char *name;
	u32 hw_flags;
} board_info[] = {
	{ "RealTek RTL8139", RTL8139_CAPS },
	{ "RealTek RTL8129", RTL8129_CAPS },
};


/*# (vendorID, deviceID, subvendor, subdevice, class, class_mask driver_data)
 *  vendorID and deviceID : This 16-bit register identifies a hardware manufacturer and This is another 16-bit register, selected by the manufacturer; no official registration is require    d for the device ID. 
 *  subvendor and subdevice:These specify the PCI subsystem vendor and subsystem device IDs of a device. If a driver can handle any type of subsystem ID, the value PCI_ANY_ID should be u    sed for these fields.
 *  class AND class_mask :These two values allow the driver to specify that it supports a type of PCI class device. The different classes of PCI devices (a VGA controller is one example)    are described in the    PCI specification. If    a driver can handle any type of subsystem ID, the value PCI_ANY_ID should be used for these fields.
 *  driver_data :This value is not used to match a device but is used to hold information that the PCI driver can use to differentiate between different devices if it wants to.
 *  Ref : http://www.makelinux.net/ldd3/chp-12-sect-1
 */
static const struct pci_device_id rtl8139_pci_tbl[] = {
	{0x10ec, 0x8139, PCI_ANY_ID, PCI_ANY_ID, 0, 0, RTL8139},
	/* some crazy cards report invalid vendor ids like
	 * 0x0001 here.  The other ids are valid and constant,
	 * so we simply don't match on the main vendor id.
	 */
	{PCI_ANY_ID, 0x8139, 0x10ec, 0x8139, 0, 0, RTL8139 },
	{0,}
};
MODULE_DEVICE_TABLE (pci, rtl8139_pci_tbl);
static struct {
	const char str[ETH_GSTRING_LEN];
} ethtool_stats_keys[] = {
	{ "early_rx" },
	{ "tx_buf_mapped" },
	{ "tx_timeouts" },
	{ "rx_lost_in_ring" },
};
/* The rest of these values should never change. */

/* Symbolic offsets to registers. */

/* New device inserted */
static int  rtl8139_init_one(struct pci_dev *dev, const struct pci_device_id *ent)
{
	pr_info("%s: probe invoked\n",__func__);
	return 0;
}
static void rtl8139_remove_one(struct pci_dev *dev)
{
	pr_info("%s: device removed safly\n",__func__);
}
static struct pci_driver rtl8139_pci_driver = {
	.name           = DRV_NAME,
	.id_table	= rtl8139_pci_tbl,
	.probe		= rtl8139_init_one,
	.remove		= rtl8139_remove_one,
};

/*register a pci driver ::- on Success return 0 otherwise errorno*/
static int __init rtl8139_init_module (void)
{
#ifdef MODULE
	pr_info(RTL8139_DRIVER_NAME "\n");
#endif
	return pci_register_driver (&rtl8139_pci_driver);
}

static void __exit rtl8139_cleanup_module (void)
{
	pci_unregister_driver (&rtl8139_pci_driver);
}

module_init(rtl8139_init_module);
module_exit(rtl8139_cleanup_module);

MODULE_LICENSE("GPL");
MODULE_AUTHOR ("Chandan Jha <beingchandanjha@gmail.com>");
MODULE_DESCRIPTION ("RealTek RTL-8139 Fast Ethernet driver");
MODULE_VERSION(DRV_VERSION);


