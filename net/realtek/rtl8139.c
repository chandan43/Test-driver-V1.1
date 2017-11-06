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
enum RTL8139_registers {
	MAC0		= 0,	 /* Ethernet hardware address. */
	MAR0		= 8,	 /* Multicast filter. */
	TxStatus0	= 0x10,	 /* Transmit status (Four 32bit registers). */
	TxAddr0		= 0x20,	 /* Tx descriptors (also four 32bit). */
	RxBuf		= 0x30,  /* Receive (Rx) Buffer Start Address*/
	ChipCmd		= 0x37,  /* Command Register*/
	RxBufPtr	= 0x38,  /* Current Address of Packet Read*/
	RxBufAddr	= 0x3A,  /* Current Buffer Address: The initial value is 0000h. It reflects total received byte-count in the rx buffer.*/
	IntrMask	= 0x3C,  /* Interrupt Mask Register*/
	IntrStatus	= 0x3E,  /* Interrupt Status Register*/
	TxConfig	= 0x40,  /* Transmit (Tx) Configuration Register*/
	RxConfig	= 0x44,  /* Receive (Rx) Configuration Register*/
	Timer		= 0x48,	 /* A 32-bit general-purpose counter.Timer CounT Register Writing any value to this 32-bit register will reset the original timer and begin to count from zero.*/
	RxMissed	= 0x4C,  /* 24 bits valid, write clears. indicates the number of packets discarded due to Rx FIFO overflow. After s/w reset, MPC is cleared. Only the lower 3 bytes are valid.*/
	Cfg9346		= 0x50,  /* 93C46 Command Register*/
	Config0		= 0x51,  /* Configuration Register 0*/
	Config1		= 0x52,  /* Configuration Register 1*/
	TimerInt	= 0x54,  /* Timer Interrupt Register.*/
	MediaStatus	= 0x58,  /* Media Status Register*/
	Config3		= 0x59,  /* Configuration register 3*/
	Config4		= 0x5A,	 /* absent on RTL-8139A  : Configuration register 4*/
	HltClk		= 0x5B,  /* Reserved*/
	MultiIntr	= 0x5C,  /* Multiple Interrupt Select*/
	TxSummary	= 0x60,  /* Transmit Status of All Descriptors*/
	BasicModeCtrl	= 0x62,  /* Basic Mode Control Register*/
	BasicModeStatus	= 0x64,  /* Basic Mode Status Register*/
	NWayAdvert	= 0x66,  /* Auto-Negotiation Advertisement Register*/
	NWayLPAR	= 0x68,  /* Auto-Negotiation Link Partner Register*/
	NWayExpansion	= 0x6A,  /* Auto-Negotiation Expansion Register*/
	/* Undocumented registers, but required for proper operation. */
	FIFOTMS		= 0x70,	 /* FIFO Control and test. : N-way Test Register*/
	CSCR		= 0x74,	 /* Chip Status and Configuration Register. */
	PARA78		= 0x78,  /* PHY parameter 1 :EPROM Reg*/
	FlashReg	= 0xD4,	/* Communication with Flash ROM, four bytes. */
	PARA7c		= 0x7c,	 /* Magic transceiver parameter register. :Twister parameter*/
	Config5		= 0xD8,	 /* absent on RTL-8139A */
};
/*MultiInt is true when MulERINT=0 (bit17, RCR). When MulERINT=1, any received packetinvokes early interrupt according to the MISR[11:0] setting in early mode*/.
/*Command Register : Bit : 0 -R- BUFE- Buffer Empty , 1-Reserved ,2-R/W-TE,Transmitter Enable: When set to 1,3-R/W-RE,Receiver Enable: When set to 1, 4-R/W-RST-Reset: Setting to 1 forces,7-5:Reserved*/
/*Config1Clear: Bit : 0 R/W PMEn - Power Management Enable(No change), 1 R/W VPD Set to enable Vital Product Data,2-R-IOMAP: I/O Mapping, 3-R-MEMMAP : Memory Mapping ,4-R/W LWACT: LWAKE active mode: 5-R/W-DV RLOAD : Driver Load, 7-6 : R/W- LEDS1-0 Refer to LED PIN definition*/
enum ClearBitMasks {
	MultiIntrClear	= 0xF000, /* Multiple Interrupt Select Register : Bit 11 to 0 , 1 to set and 0 for Clear , 15-12 : Reserved*/   
	ChipCmdClear	= 0xE2,   /* Reset: 111(bit 5-7)0001(bit 1)0 ,Reserved bit will be set ,otherwise clear*/ 
	Config1Clear	= (1<<7)|(1<<6)|(1<<3)|(1<<2)|(1<<1), /*Bit 0,4,5 values are bydefault 0 so no need to change otherwise flip*/ 
};
enum ChipCmdBits {
	CmdReset	= 0x10,  /*Bit : 4-R/W-RST-Reset: Setting to 1 forces*/
	CmdRxEnb	= 0x08,  /*Bit : 3-R/W-RE,Receiver Enable: When set to 1*/
	CmdTxEnb	= 0x04,  /*BIT : 2-R/W-TE,Transmitter Enable: When set to 1 */
	RxBufEmpty	= 0x01,  /*Bit : 0 -R- BUFE- Buffer Empty  */
};
/* Interrupt register bits, using my own meaningful names. */
enum IntrStatusBits {
	PCIErr		= 0x8000,/*BIT : 15-R/W-SERR :Set to 1 when the RTL8139D(L) signals a system error on the PCI bus.*/ 
	PCSTimeout	= 0x4000,/*BIT : 14 R/W TimeOut: Set to 1 when the TCTR register reaches to the value of the TimerInt register.*/ 
	RxFIFOOver	= 0x40,  /*BIT : 6 R/W FOVW: Rx FIFO Overflow: Set when an overflow occurs on the Rx status FIFO.*/
	RxUnderrun	= 0x20,  /*BIT : 5 R/W PUN/LinkChg : Packet Underrun/Link Change: Set to 1 when CAPR is written but Rx buffer is empty, or when link status is changed.*/
	RxOverflow	= 0x10,  /*BIT : 4 R/W RXOVW : Rx Buffer Overflow: Set when receive (Rx) buffer ring storage resources have been exhausted.*/
	TxErr		= 0x08,  /*BIT : 3 R/W TER : Transmit (Tx) Error: Indicates that a packet transmission was aborted, due to excessive collisions, according to the TXRR's setting.*/
	TxOK		= 0x04,  /*BIT : 2 R/W TOK : Transmit (Tx) OK: Indicates that a packet transmission is completed successfully.*/
	RxErr		= 0x02,  /*BIT : 1 R/W RER : Receive (Rx) Error: Indicates that a packet has either CRC error or frame alignment error (FAE). */
	RxOK		= 0x01,  /*BIT : 0 R/W ROK : Receive (Rx) OK: In normal mode, indicates the successful completion of a packet reception.*/

	RxAckBits	= RxFIFOOver | RxOverflow | RxOK,
};
/*1. BIT : 13 R/W OWN The RTL8139D(L) sets this bit to 1 when the Tx DMA operation of this descriptor was completed. The driver must set this bit to 0 when the Transmit Byte Count (bits 0-12) is written. The     default value is 1.
  2. BIT : 14 R/W SIZE : Transmit FIFO Underrun: Set to 1 if the Tx FIFO was exhausted during the transmission of a packet.
  3. BIT : 15 R/W TOK  : Transmit OK: Set to 1 indicates that the transmission of a packet was completed successfully and no transmit underrun has occurred.
  4. BIT : 29 R OWC    : Out of Window Collision: This bit is set to 1 if the RTL8139D(L) encountered an "out of window" collision during the transmission of a packet.
  5. BIT : 30 R TABT   : Transmit Abort: This bit is set to 1 if the transmission of a packet was aborted. This bit is read only, writing to this bit is not affected.
  6. BIT : 31 R CRS    : Carrier Sense Lost: This bit is set to 1 when the carrier is lost during transmission of a packet.   
*/ 
enum TxStatusBits {
	TxHostOwns	= 0x2000, 
	TxUnderrun	= 0x4000, 
	TxStatOK	= 0x8000,
	TxOutOfWindow	= 0x20000000,
	TxAborted	= 0x40000000,
	TxCarrierLost	= 0x80000000,
};
/*
 1. BIT : 15 R MAR : Multicast Address Received: This bit set to 1 indicates that a multicast packet is received.
 2. BIT : 14 R PAM : Physical Address Matched: This bit set to 1 indicates that the destination address of this packet matches the value written in ID registers.
 3. BIT : 13 R BAR : Broadcast Address Received: This bit set to 1 indicates that a broadcast packet is received. BAR, MAR bit will not be set simultaneously. 
 4. BIT : 5 R ISE  : Invalid Symbol Error: (100BASE-TX only) This bit set to 1 indicates that an invalid symbol was encountered during the reception of this packet. 
 5. BIT : 4 R RUNT : Runt Packet Received: This bit set to 1 indicates that the received packet length is smaller than 64 bytes ( i.e. media header + data + CRC < 64 bytes )
 6. BIT : 3 R LONG : Long Packet: This bit set to 1 indicates that the size of the received packet exceeds 4k bytes.
 7. BIT : 2 R CRC  : CRC Error: When set, indicates that a CRC error occurred on the received packet.
 8. BIT : 1 R FAE  : Frame Alignment Error: When set, indicates that a frame alignment error occurred on this received packet.
 9. BIT : 0 R ROK  : Receive OK: When set, indicates that a good packet is received.
*/
enum RxStatusBits {
	RxMulticast	= 0x8000,
	RxPhysical	= 0x4000,
	RxBroadcast	= 0x2000,
	RxBadSymbol	= 0x0020,
	RxRunt		= 0x0010,
	RxTooLong	= 0x0008,
	RxCRCErr	= 0x0004,
	RxBadAlign	= 0x0002,
	RxStatusOK	= 0x0001,
};
/* Bits in TxConfig. */
/* Interframe Gap Time: This field allows the user to adjust the interframe gap time below the standard: 9.6 us for 10Mbps, 960 ns for 100Mbps. The time can be programmed from 9.6 us to 8.4 us (10Mbps)
   and 960ns to 840ns (100Mbps). Note that any value other than (1, 1) will violate the IEEE 802.3 standard. The formula for the inter frame gap is:
   10 Mbps:  8.4us + 0.4(IFG(1:0)) us , 100 Mbps: 840ns + 40(IFG(1:0)) ns
   BIT 18, 17 : R/W LBK1, LBK0 : 00 : normal operation , 01 : Reserved , 10 : Reserved 11 : Loopback mode
   BIT 0 W CLRABT : Clear Abort: Setting this bit to 1 causes the RTL8139D(L) to retransmit the packet at the last transmitted descriptor when this transmission was aborted, Setting this bit is only 
   permitted in the transmit abort state. 
*/
enum tx_config_bits {
        /* Interframe Gap Time. Only TxIFG96 doesn't violate IEEE 802.3 */
        TxIFGShift	= 24,                /* BIT 24-25 IFG1, 0 : R/W Interframe Gap Time: */  
        TxIFG84		= (0 << TxIFGShift), /* 8.4us / 840ns (10 / 100Mbps) */
        TxIFG88		= (1 << TxIFGShift), /* 8.8us / 880ns (10 / 100Mbps) */
        TxIFG92		= (2 << TxIFGShift), /* 9.2us / 920ns (10 / 100Mbps) */
        TxIFG96		= (3 << TxIFGShift), /* 9.6us / 960ns (10 / 100Mbps) */

	TxLoopBack	= (1 << 18) | (1 << 17), /* enable loopback test mode */
	TxCRC		= (1 << 16),	/* DISABLE Tx pkt CRC append */
	TxClearAbt	= (1 << 0),	/* Clear abort (WO) */
	TxDMAShift	= 8, /* DMA burst value (0-7) is shifted X many bits */
	TxRetryShift	= 4, /* TXRR value (0-15) is shifted X many bits */

	TxVersionMask	= 0x7C800000, /* mask out version bits 30-26, 23 0111   */
};
/* Bits in Config1 */
/*Config1Bits: 
              Bit : 0 R/W PMEn - Power Management Enable, 
                    1 R/W VPD Set to enable Vital Product Data,
                    2-R-IOMAP: I/O Mapping, 
                    3-R-MEMMAP : Memory Mapping ,
                    4-R/W LWACT: LWAKE active mode: 
                    5-R/W-DV RLOAD : Driver Load, 
                    7-6 : R/W- LEDS1-0 Refer to LED PIN definition
*/
enum Config1Bits {
	Cfg1_PM_Enable	= 0x01,
	Cfg1_VPD_Enable	= 0x02,
	Cfg1_PIO	= 0x04,
	Cfg1_MMIO	= 0x08,
	LWAKE		= 0x10,		/* not on 8139, 8139A */
	Cfg1_Driver_Load = 0x20,
	Cfg1_LED0	= 0x40,
	Cfg1_LED1	= 0x80,
	SLEEP		= (1 << 1),	/* only on 8139, 8139A : Set to enable Vital Product Data: The VPD data is stored in 93C46 from within offset 40h-7Fh.*/
	PWRDN		= (1 << 0),	/* only on 8139, 8139A : 1  means A(bit 4 of the Status Register) in the PCI)=1, B(Cap_Ptr register)=50h, C(power management)=01h, D(PM registers) valid, E=0*/
};


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


