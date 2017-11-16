#include <linux/module.h>
#include <linux/init.h>
#include <linux/compiler.h>
#include <linux/pci.h>
#include <linux/etherdevice.h>
#include <linux/netdevice.h>
#include <linux/ethtool.h>
#include <linux/if_vlan.h>
#include <linux/rtnetlink.h>
#include <linux/delay.h>
#include <linux/mii.h>

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
/* media options  
 * (-) indicates, media and duplex will reset when link-status goes down.*/
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
	Cfg1_Driver_Load= 0x20,
	Cfg1_LED0	= 0x40,
	Cfg1_LED1	= 0x80,
	SLEEP		= (1 << 1),/* only on 8139, 8139A : Set to enable Vital Product Data: The VPD data is stored in 93C46 from within offset 40h-7Fh.*/
	PWRDN		= (1 << 0),/* only on 8139, 8139A : 1  means A(bit 4 of the Status Register) in the PCI)=1, B(Cap_Ptr register)=50h, C(power management)=01h, D(PM registers) valid, E=0*/
};

/* Bits in Config3 */
enum Config3Bits {
	Cfg3_FBtBEn   	= (1 << 0), /* 1	= Fast Back to Back */
	Cfg3_FuncRegEn	= (1 << 1), /* 1	= enable CardBus Function registers */
	Cfg3_CLKRUN_En	= (1 << 2), /* 1	= enable CLKRUN */
	Cfg3_CardB_En 	= (1 << 3), /* 1	= enable CardBus registers */
	Cfg3_LinkUp   	= (1 << 4), /* 1	= wake up on link up */
	Cfg3_Magic    	= (1 << 5), /* 1	= wake up on Magic Packet (tm) */
	Cfg3_PARM_En  	= (1 << 6), /* 0	= software can set twister parameters */
	Cfg3_GNTSel   	= (1 << 7), /* 1	= delay 1 clock from PCI GNT signal */
};

/* Bits in Config4 */
enum Config4Bits {
	LWPTN	= (1 << 2),	/* not on 8139, 8139A : LWAKE pattern: Please refer to LWACT bit : 4 in CONFIG1 register. */
};

/* Bits in Config5 */
enum Config5Bits {
	Cfg5_PME_STS   	= (1 << 0), /* 1	= PCI reset resets PME_Status */
	Cfg5_LANWake   	= (1 << 1), /* 1	= enable LANWake signal */
	Cfg5_LDPS      	= (1 << 2), /* 0	= save power when link is down */
	Cfg5_FIFOAddrPtr= (1 << 3), /* Realtek internal SRAM testing : 1: Both Rx and Tx FIFO address pointers are updated in descending , */
	Cfg5_UWF        = (1 << 4), /* 1 = accept unicast wakeup frame */
	Cfg5_MWF        = (1 << 5), /* 1 = accept multicast wakeup frame */
	Cfg5_BWF        = (1 << 6), /* 1 = accept broadcast wakeup frame */
};
/*
1.Rx FIFO Threshold: 15-13 R/W RXFTH2, 1, 0   Specifies Rx FIFO Threshold level ,Whenever Rx FIFO, has reached to this level the receive PCI bus master function
  will begin to transfer the data from the FIFO to the host memory . 
  111 = no rx threshold. begins the transfer of data after having received a whole packet in the FIFO
2.Max DMA Burst Size per Rx DMA Burst: 10-8 R/W MXDMA2, 1, 0, This field sets the maximum size of the receive DMA data bursts according to the following table:
  111 = unlimited
3.Rx Buffer Length: 12-11 R/W RBLEN1, 0  ,This field indicates the size of the Rx ring buffer. 00 = 8k + 16 byte 01 = 16k + 16 byte 10 = 32K + 16 byte 11 = 64K + 16 byte
*/
enum RxConfigBits {
	/* rx fifo threshold */
	RxCfgFIFOShift	= 13,
	RxCfgFIFONone	= (7 << RxCfgFIFOShift),

	/* Max DMA burst */
	RxCfgDMAShift	= 8,
	RxCfgDMAUnlimited = (7 << RxCfgDMAShift),

	/* rx ring buffer length */
	RxCfgRcv8K	= 0,
	RxCfgRcv16K	= (1 << 11),
	RxCfgRcv32K	= (1 << 12),
	RxCfgRcv64K	= (1 << 11) | (1 << 12),

	/* Disable packet wrap at end of Rx buffer. (not possible with 64k) */
	RxNoWrap	= (1 << 7),
};
/* Twister tuning parameters from RealTek.
   Completely undocumented, but required to tune bad links on some boards. */
/*
15        Testfun                1 = Auto-neg speeds up internal timer
14-10       -                    Reserved
 9          LD                   Active low TPI link disable signal. When low, TPI still transmits
                                         link pulses and TPI stays in good link state.
 8        HEART BEAT             1 = HEART BEAT enable, 0 = HEART BEAT disable. HEART
 7            JBEN               BEAT function is only valid in 10Mbps mode.
                                        1 = enable jabber function; 0 = disable jabber function
 6        F_LINK_100              Used to login force good link in 100Mbps for diagnostic purposes.
                                         1 = DISABLE, 0 = ENABLE.
 5         F_Connect              Assertion of this bit forces the disconnect function to be bypassed.
 4           -                    Reserved
 3        Con_status              This bit indicates the status of the connection. 1 = valid connected
                                  link detected; 0 = disconnected link detected.
 2        Con_status_En           Assertion of this bit configures LED1 pin to indicate connection
                                  status.
 1             -                  Reserved
 0          PASS_SCR              Bypass Scramble
 */
enum CSCRBits {
	CSCR_LinkOKBit		= 0x0400,
	CSCR_LinkChangeBit	= 0x0800,
	CSCR_LinkStatusBits	= 0x0f000,
	CSCR_LinkDownOffCmd	= 0x003c0,
	CSCR_LinkDownCmd	= 0x0f3c0,
}
/*
  9346CR: 93C46 Command Register : Bit 7-6 , R/W EEM1-0 ,Operating Mode: These 2 bits select the RTL8139D(L) operating mode, 
  00 : Normal (RTL8139D(L) network/host communication
  01 : Auto-load: Entering this mode
  10 : 93C46 programming
  11 : Config register write enable: Before writing to CONFIG0, 1, 3, 4 registers, and bit13, 12, 8 of BMCR(offset 62h-63h), the RTL8139D(L) must be placed in this mode.
       This will prevent RTL8139D(L)'s configurations from accidental change
*/
enum Cfg9346Bits {
	Cfg9346_Lock	= 0x00,
	Cfg9346_Unlock	= 0xC0,
};

typedef enum {
	CH_8139	= 0,
	CH_8139_K,
	CH_8139A,
	CH_8139A_G,
	CH_8139B,
	CH_8130,
	CH_8139C,
	CH_8100,
	CH_8100B_8139D,
	CH_8101,
} chip_t
/*
  Media Status Register : 
			HasHltClk: BIT 0 R RXPF , Receive Pause Flag: Set, when RTL8139D(L) is in backoff state because a pause packet was received. Reset, when pause state is clear.
			HasLWake : Bit 1 R TXPF , Transmit Pause Flag: Set, when RTL8139D(L) sends pause packet. Reset, when RTL8139D(L) sends a timer done packet.
*/
enum chip_flags {
	HasHltClk	= (1 << 0),
	HasLWake	= (1 << 1),
};
#define HW_REVID(b30, b29, b28, b27, b26, b23, b22)  (b30<<30 | b29<<29 | b28<<28 | b27<<27 | b26<<26 | b23<<23 | b22<<22)
#define HW_REVID_MASK	HW_REVID(1, 1, 1, 1, 1, 1, 1)

/* directly indexed by chip_t, above */
static const struct {
	const char *name;
	u32 version; /* from RTL8139C/RTL8139D docs */
	u32 flags;
} rtl_chip_info[] = {
	{ "RTL-8139",
	  HW_REVID(1, 0, 0, 0, 0, 0, 0),
	  HasHltClk,
	},

	{ "RTL-8139 rev K",
	  HW_REVID(1, 1, 0, 0, 0, 0, 0),
	  HasHltClk,
	},

	{ "RTL-8139A",
	  HW_REVID(1, 1, 1, 0, 0, 0, 0),
	  HasHltClk, /* XXX undocumented? */
	},

	{ "RTL-8139A rev G",
	  HW_REVID(1, 1, 1, 0, 0, 1, 0),
	  HasHltClk, /* XXX undocumented? */
	},

	{ "RTL-8139B",
	  HW_REVID(1, 1, 1, 1, 0, 0, 0),
	  HasLWake,
	},

	{ "RTL-8130",
	  HW_REVID(1, 1, 1, 1, 1, 0, 0),
	  HasLWake,
	},

	{ "RTL-8139C",
	  HW_REVID(1, 1, 1, 0, 1, 0, 0),
	  HasLWake,
	},

	{ "RTL-8100",
	  HW_REVID(1, 1, 1, 1, 0, 1, 0),
 	  HasLWake,
 	},

	{ "RTL-8100B/8139D",
	  HW_REVID(1, 1, 1, 0, 1, 0, 1),
	  HasHltClk /* XXX undocumented? */
	| HasLWake,
	},

	{ "RTL-8101",
	  HW_REVID(1, 1, 1, 0, 1, 1, 1),
	  HasLWake,
	},
};

struct rtl_extra_stats {
	unsigned long early_rx;
	unsigned long tx_buf_mapped;
	unsigned long tx_timeouts;
	unsigned long rx_lost_in_ring;
};
struct rtl8139_stats {
	u64	packets;
	u64	bytes;
	struct u64_stats_sync	syncp;
};

struct rtl8139_private {
	void __iomem		*mmio_addr;  /*memory mapped I/O addr */
	int			drv_flags;    
	struct pci_dev		*pci_dev;    /*PCI device */
	u32			msg_enable; 
	struct napi_struct	napi; /* Structure for NAPI scheduling similar to tasklet but with weighting */ 
	struct net_device	*dev; /*  The NET DEVICE structure.*/

	unsigned char		*rx_ring;
	unsigned int		cur_rx;	  /* RX buf index of next pkt */
	struct rtl8139_stats	rx_stats;
	dma_addr_t		rx_ring_dma;

	unsigned int		tx_flag;  /*tx_flag shall contain transmission flags to notify the device*/
	unsigned long		cur_tx;   /*cur_tx shall hold current transmission descriptor*/
	unsigned long		dirty_tx; /*dirty_tx denotes the first of transmission descriptors which have not completed transmission.*/
	struct rtl8139_stats	tx_stats;  
	unsigned char		*tx_buf[NUM_TX_DESC];	/* Tx bounce buffers */
	unsigned char		*tx_bufs;	/* Tx bounce buffer region. */
	dma_addr_t		tx_bufs_dma;

	signed char		phys[4];	/* MII deive_skb (skb); addresses. */

				/* Twister tune state. */
	char			twistie, twist_row, twist_col;

	unsigned int		watchdog_fired : 1;
	unsigned int		default_port : 4; /* Last dev->if_port value. */
	unsigned int		have_thread : 1;

	spinlock_t		lock;
	spinlock_t		rx_lock;

	chip_t			chipset;
	u32			rx_config;
	struct rtl_extra_stats	xstats;

	struct delayed_work	thread;  /*types of work structure */
 
	struct mii_if_info	mii;   /*Media Independent Interface Support : Ethtool Support*/
	unsigned int		regs_len;
	unsigned long		fifo_copy_timeout;
};
module_param(use_io, bool, 0);
MODULE_PARM_DESC(use_io, "Force use of I/O access mode. 0=MMIO 1=PIO");
module_param(multicast_filter_limit, int, 0);
module_param_array(media, int, NULL, 0);
module_param_array(full_duplex, int, NULL, 0);
module_param(debug, int, 0);
MODULE_PARM_DESC (debug, "8139too bitmapped message enable number");
MODULE_PARM_DESC (multicast_filter_limit, "8139too maximum number of filtered multicast addresses");
MODULE_PARM_DESC (media, "8139too: Bits 4+9: force full duplex, bit 5: 100Mbps");
MODULE_PARM_DESC (full_duplex, "8139too: Force full duplex for board(s) (1)");


static int read_eeprom (void __iomem *ioaddr, int location, int addr_len);
/* write MMIO register, with flush */
/* Flush avoids rtl8139 bug w/ posted MMIO writes */
#define RTL_W8_F(reg, val8)	do { iowrite8 ((val8), ioaddr + (reg)); ioread8 (ioaddr + (reg)); } while (0)
#define RTL_W16_F(reg, val16)	do { iowrite16 ((val16), ioaddr + (reg)); ioread16 (ioaddr + (reg)); } while (0)
#define RTL_W32_F(reg, val32)	do { iowrite32 ((val32), ioaddr + (reg)); ioread32 (ioaddr + (reg)); } while (0)

/* write MMIO register */
#define RTL_W8(reg, val8)	iowrite8 ((val8), ioaddr + (reg))
#define RTL_W16(reg, val16)	iowrite16 ((val16), ioaddr + (reg))
#define RTL_W32(reg, val32)	iowrite32 ((val32), ioaddr + (reg))

/* read MMIO register */
#define RTL_R8(reg)		ioread8 (ioaddr + (reg))
#define RTL_R16(reg)		ioread16 (ioaddr + (reg))
#define RTL_R32(reg)		ioread32 (ioaddr + (reg))

static const u16 rtl8139_intr_mask =
	PCIErr | PCSTimeout | RxUnderrun | RxOverflow | RxFIFOOver |
	TxErr | TxOK | RxErr | RxOK;

static const u16 rtl8139_norx_intr_mask =
	PCIErr | PCSTimeout | RxUnderrun |
	TxErr | TxOK | RxErr ;

/* RxCfgRcv8K : 8K/16K/32K/64K | NO wrap | RX_FIFO_THRESH(7) << 14 i.e no rx threshold| RX_DMA_BURST(7) << 8 i.e unlimited DMA data bursts */
#if RX_BUF_IDX == 0
static const unsigned int rtl8139_rx_config =
	RxCfgRcv8K | RxNoWrap |
	(RX_FIFO_THRESH << RxCfgFIFOShift) |
	(RX_DMA_BURST << RxCfgDMAShift);
#elif RX_BUF_IDX == 1
static const unsigned int rtl8139_rx_config =
	RxCfgRcv16K | RxNoWrap |
	(RX_FIFO_THRESH << RxCfgFIFOShift) |
	(RX_DMA_BURST << RxCfgDMAShift);
#elif RX_BUF_IDX == 2
static const unsigned int rtl8139_rx_config =
	RxCfgRcv32K | RxNoWrap |
	(RX_FIFO_THRESH << RxCfgFIFOShift) |
	(RX_DMA_BURST << RxCfgDMAShift);
#elif RX_BUF_IDX == 3
static const unsigned int rtl8139_rx_config =
	RxCfgRcv64K |
	(RX_FIFO_THRESH << RxCfgFIFOShift) |
	(RX_DMA_BURST << RxCfgDMAShift);
#else
#error "Invalid configuration for 8139_RXBUF_IDX"
#endif
/* rtl8139_tx_config : TxIFG96 () | (TX_DMA_BURST (6)<< TxDMAShift(8)) i.e 1024 | TX_RETRY(8 = 1000) << TxRetryShift (4) : retry =16*/
static const unsigned int rtl8139_tx_config =
	TxIFG96 | (TX_DMA_BURST << TxDMAShift) | (TX_RETRY << TxRetryShift);
/*
 -pci_release_regions: Release reserved PCI I/O and memory resources,Releases all PCI I/O and memory resources previously reserved by a successful call to pci_request_regions. Call this   function only after all use of the PCI regions has ceased.   
*/
/**
 * free_netdev - free network device
 * @dev: device
 *
 * This function does the last stage of destroying an allocated device
 * interface. The reference to the device object is released. If this
 * is the last reference then it will be freed.Must be called in process
 * context.
 */
static void __rtl8139_cleanup_dev (struct net_device *dev)
{
	struct rtl8139_private *tp = netdev_priv(dev);
	struct pci_dev *pdev;
	assert (dev != NULL);
	assert (tp->pci_dev != NULL);
	pdev = tp->pci_dev;
	/*Before release pci region we have to unmap */
	if (tp->mmio_addr)
		pci_iounmap (pdev, tp->mmio_addr);
	/* it's ok to call this even if we have no regions to free */
	pci_release_regions (pdev);

	free_netdev(dev);
	
}
static void rtl8139_chip_reset (void __iomem *ioaddr)
{
	int i;
	
	/* Soft reset the chip. */
	RTL_W8 (ChipCmd, CmdReset);
	/* Check that the chip has finished the reset. */
	/* the memory barrier is needed to ensure that the reset happen in the expected order.*/
	for (i = 1000; i > 0; i--) {
		barrier();
		if ((RTL_R8 (ChipCmd) & CmdReset) == 0)
			break;
		udelay (10);
	}
}
/*
  -alloc_etherdev: Allocates and sets up an Ethernet device , Fill in the fields of the device structure with Ethernet-generic values. Basically does everything except registering the 
   device. Constructs a new net device, complete with a private data area of size (sizeof_priv). A 32-byte (not bit) alignment is enforced for this private data area. 
  -SET_NETDEV_DEV: Set the sysfs physical device reference for the network logical device if set prior to registration will cause a symlink during initialization.
   SET_NETDEV_DEV (net, pdev): Sets the parent of the 
  -dev member of the specified  network device to be that specified device (the second argument,  pdev). With virtual devices, you do not call the SET_NETDEV_DEV() macro. As a result, 
   entries for these virtual devices are created under /sys/devices/virtual/net 
   The SET_NETDEV_DEV() macro should be called before calling the  register_netdev() method.
 - netdev_priv - access network device private data
   @dev: network device ,Get network device private data
 - pci_enable_device — Initialize device before it's used by a driver.Ask low-level code to enable I/O and memory. Wake up the device if it was suspended. Beware, this function can fail.
   Note we don't actually enable the device many times if we call this function repeatedly (we just increment the count). 
 - pci_request_regions — Reserved PCI I/O and memory resources , Mark all PCI regions associated with PCI device pdev as being reserved by owner res_name. Do not access any address 
   inside the PCI regions unless this call returns successfully. Returns 0 on success, or EBUSY on error. A warning message is also printed on failure.
 - pci_set_master — enables bus-mastering for device dev , Enables bus-mastering on the device and calls pcibios_set_master to do the needed arch specific settings. 
 - u64_stats_init: static inline void u64_stats_init(struct u64_stats_sync *syncp) : seqcount reads sequence entries and reports the number of entries found.  
 - pci_resource_len() :Returns the byte length of a PCI region 
*/
/* #define IORESOURCE_IO           0x00000100      PCI/ISA I/O ports    */
/* #define IORESOURCE_MEM          0x00000200             */
/*
	Step 1: alloc_etherdev
	Step 2: if success step 1 , then SET_NETDEV_DEV   //optional
	Step 3: Access network device private data and set device 
	Step 4: Enable device using pci_enable_device , Reserved PCI I/O and memory using pci_request_regions , set master pci_set_master using bus-mastering for device dev 
	Step 5: Check device config and init. 
*/	
static struct net_device *rtl8139_init_board(struct pci_dev *pdev)
{
	struct device *d = &pdev->dev;
	void __iomem *ioaddr;
	struct net_device *dev;
	struct rtl8139_private *tp;   /*private data*/
	u8 tmp8;
	int rc, disable_dev_on_err = 0;
	unsigned int i, bar;
	unsigned long io_len;
	u32 version;
	static const struct {
		unsigned long mask;
		char *type;
	} res[] = {
		{ IORESOURCE_IO,  "PIO" },
		{ IORESOURCE_MEM, "MMIO" }
	};

	assert (pdev != NULL);
	
	/* dev and priv zeroed in alloc_etherdev */
	dev = alloc_etherdev (sizeof (*tp));
	if(dev == NULL)
		return ERR_PTR(-ENOMEM);
	/*As a result,entries for these virtual devices are created under /sys/devices/virtual/net*/
	SET_NETDEV_DEV(dev, &pdev->dev);  
	tp = netdev_priv(dev);
	tp->pci_dev = pdev;	
	/* enable device (incl. PCI PM wakeup and hotplug setup) */
	rc = pci_enable_device (pdev);
	if (rc)
		goto err_out;
	disable_dev_on_err = 1;
	rc = pci_request_regions (pdev, DRV_NAME);
	if (rc)
		goto err_out;
	pci_set_master (pdev);
	u64_stats_init(&tp->rx_stats.syncp);
	u64_stats_init(&tp->tx_stats.syncp);
retry:
	/* PIO bar register comes first. */
	bar = !use_io;
	io_len = pci_resource_len(pdev, bar);
	dev_dbg(d, "%s region size = 0x%02lX\n", res[bar].type, io_len);
	/*pci_resource_flags : This function returns the flags associated with this resource */
	if (!(pci_resource_flags(pdev, bar) & res[bar].mask)) {
		dev_err(d, "region #%d not a %s resource, aborting\n", bar,
			res[bar].type);
		rc = -ENODEV;
		goto err_out;
	}
	if (io_len < RTL_MIN_IO_SIZE) {
		dev_err(d, "Invalid PCI %s region size(s), aborting\n",
			res[bar].type);
		rc = -ENODEV;
		goto err_out;
	}
	/* Create a virtual mapping cookie for a PCI BAR (memory or IO) */
	ioaddr = pci_iomap(pdev, bar, 0);
	if (!ioaddr) {
		dev_err(d, "cannot map %s\n", res[bar].type);
		if (!use_io) {
			use_io = true;
			goto retry;
		}
		rc = -ENODEV;
		goto err_out;
	}
	tp->regs_len = io_len;
	tp->mmio_addr = ioaddr;
	
	/* Bring old chips out of low-power mode. */
	RTL_W8 (HltClk, 'R');
	/* check for missing/broken hardware */
	if (RTL_R32 (TxConfig) == 0xFFFFFFFF) {
		dev_err(&pdev->dev, "Chip not responding, ignoring board\n");
		rc = -EIO;
		goto err_out;
	}
	/* identify chip attached to board */
	version = RTL_R32 (TxConfig) & HW_REVID_MASK;
	for (i = 0; i < ARRAY_SIZE (rtl_chip_info); i++)
		if (version == rtl_chip_info[i].version) {
			tp->chipset = i;
			goto match;
		}
	/* if unknown chip, assume array element #0, original RTL-8139 in this case */
	i = 0;
	dev_dbg(&pdev->dev, "unknown chip version, assuming RTL-8139\n");
	dev_dbg(&pdev->dev, "TxConfig = 0x%x\n", RTL_R32 (TxConfig));
	tp->chipset = 0;

match:
	pr_debug("chipset id (%d) == index %d, '%s'\n",
		 version, i, rtl_chip_info[i].name);
	if(tp->chipset >=  CH_8139B){
		u8 new_tmp8 = tmp8 = RTL_R8 (Config1);
		pr_debug("PCI PM wakeup\n");
		if ((rtl_chip_info[tp->chipset].flags & HasLWake) &&
		    (tmp8 & LWAKE))
			new_tmp8 &= ~LWAKE;             /*Removing LWAKE bit from new_tmp8*/
			new_tmp8 |= Cfg1_PM_Enable;      /*Enable power Management*/
			if(new_tmp8 != tmp8 ){
				RTL_W8 (Cfg9346, Cfg9346_Unlock);	
				RTL_W8 (Config1, tmp8);
				RTL_W8 (Cfg9346, Cfg9346_Lock);	
			}
			if (rtl_chip_info[tp->chipset].flags & HasLWake) {
				tmp8 = RTL_R8 (Config4);
				if (tmp8 & LWPTN) {
					RTL_W8 (Cfg9346, Cfg9346_Unlock);
					RTL_W8 (Config4, tmp8 & ~LWPTN);
					RTL_W8 (Cfg9346, Cfg9346_Lock);
				}
			}
						
	}else{
		pr_debug("Old chip wakeup\n");
		tmp8 = RTL_R8 (Config1);
		tmp8 &= ~(SLEEP | PWRDN);
		RTL_W8 (Config1, tmp8);
	}
	rtl8139_chip_reset (ioaddr);
	return dev;	
err_out:
	__rtl8139_cleanup_dev (dev);
	if (disable_dev_on_err)
		pci_disable_device (pdev);
	return ERR_PTR(rc);
}
static int rtl8139_set_features(struct net_device *dev, netdev_features_t features)
{
	struct rtl8139_private *tp = netdev_priv(dev);
	unsigned long flags;
	
	netdev_features_t changed = features ^ dev->features;		
	void __iomem *ioaddr = tp->mmio_addr;
	
	if (!(changed & (NETIF_F_RXALL)))
		return 0;
	spin_lock_irqsave(&tp->lock, flags);
	
	if (changed & NETIF_F_RXALL) {
		int rx_mode = tp->rx_config;
		if (features & NETIF_F_RXALL)
			rx_mode |= (AcceptErr | AcceptRunt);
		else
			rx_mode &= ~(AcceptErr | AcceptRunt);
		tp->rx_config = rtl8139_rx_config | rx_mode;
		RTL_W32_F(RxConfig, tp->rx_config);
	}
	spin_unlock_irqrestore(&tp->lock, flags);

	return 0;	
}
static int rtl8139_change_mtu(struct net_device *dev, int new_mtu)
{
	if (new_mtu < 68 || new_mtu > MAX_ETH_DATA_SIZE)
		return -EINVAL;
	dev->mtu = new_mtu;
	return 0;
}

static const struct net_device_ops rtl8139_netdev_ops = {
	.ndo_open		= rtl8139_open,
	.ndo_stop		= rtl8139_close,
	.ndo_get_stats64	= rtl8139_get_stats64,
	.ndo_change_mtu		= rtl8139_change_mtu,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_mac_address 	= rtl8139_set_mac_address,
	.ndo_start_xmit		= rtl8139_start_xmit,
	.ndo_set_rx_mode	= rtl8139_set_rx_mode,
	.ndo_do_ioctl		= netdev_ioctl,
	.ndo_tx_timeout		= rtl8139_tx_timeout,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	= rtl8139_poll_controller,
#endif
	.ndo_set_features	= rtl8139_set_features,
};
/**
 *	netif_napi_add - initialize a NAPI context
 *	@dev:  network device
 *	@napi: NAPI context
 *	@poll: polling function
 *	@weight: default weight
 *
 * netif_napi_add() must be used to initialize a NAPI context prior to calling
 * *any* of the other NAPI-related functions.
 */
/* New device inserted : probe function -*/
/**
 *  netif_napi_del - remove a napi context
 *  @napi: napi context
 *
 *  netif_napi_del() removes a napi context from the network device napi list
 */
static int  rtl8139_init_one(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct net_device *dev = NULL;
	struct rtl8139_private *tp;                /*our private data structures,*/
	int i, addr_len, option; 
	void __iomem *ioaddr;                      /*IO MEM Addresse*/
	static int board_idx = -1;
	assert (pdev != NULL);
	assert (ent != NULL );
	board_idx++;
	
	/* when we're built into the kernel, the driver version message
	 * is only printed if at least one 8139 board has been found
	 */
#ifndef MODULE
	{
		static int printed_version;
		if (!printed_version++)
			pr_info(RTL8139_DRIVER_NAME "\n");
	}
#endif
	/*pdev->revision:  0x20 for enhanced 8139C+ version, revision for realtek card is less then 0x20 */ 
	if(pdev->vendor == PCI_VENDOR_ID_REALTEK && 
		pdev->device == PCI_DEVICE_ID_REALTEK_8139 && pdev->revision >= 0x20){
		dev_info(&pdev->dev,
			   "this (id %04x:%04x rev %02x) is an enhanced 8139c+ chip, use 8139cp\n",
		       	   pdev->vendor, pdev->device, pdev->revision);
		return -ENODEV;
	}
	if (pdev->vendor == PCI_VENDOR_ID_REALTEK &&
	    pdev->device == PCI_DEVICE_ID_REALTEK_8139 &&
	    pdev->subsystem_vendor == PCI_VENDOR_ID_ATHEROS &&
	    pdev->subsystem_device == PCI_DEVICE_ID_REALTEK_8139) {
		pr_info("OQO Model 2 detected. Forcing PIO\n");
		dev_info(&pdev->dev,
			   "This (id %04x:%04x rev %02x) detected.\n",
		       	   pdev->vendor, pdev->device, pdev->revision);
		use_io = 1;
	}
	dev = rtl8139_init_board (pdev);
	if (IS_ERR(dev))
		return PTR_ERR(dev);
	assert (dev != NULL);
	tp = netdev_priv(dev);
	tp->dev = dev;
	ioaddr = tp->mmio_addr;
	assert (ioaddr != NULL);
	addr_len = read_eeprom (ioaddr, 0, 8) == 0x8129 ? 8 : 6;
	/* Interface address info used in eth_type_trans() */
	/* unsigned char		*dev_addr;	hw address, (before bcast because most packets are unicast) */
	for (i = 0; i < 3; i++)
		((__le16 *) (dev->dev_addr))[i] =
		    cpu_to_le16(read_eeprom (ioaddr, i + 7, addr_len));
	/* The Rtl8139-specific entries in the device structure. */
	dev->netdev_ops = &rtl8139_netdev_ops;
#if 0
	dev->ethtool_ops = &rtl8139_ethtool_ops;
#endif
	dev->watchdog_timeo = TX_TIMEOUT;
	netif_napi_add(dev, &tp->napi, rtl8139_poll, 64); /* Initialize a NAPI context */	
	
	/* note: the hardware is not capable of sg/csum/highdma, however
	 * through the use of skb_copy_and_csum_dev we enable these
	 * features
	 */
	/* NETIF_F_SG_BIT : Scatter/gather IO. NETIF_F_HW_CSUM : Can checksum all the packets. NETIF_F_HIGHDMA :  Can DMA to high memory. */
	dev->features |= NETIF_F_SG | NETIF_F_HW_CSUM | NETIF_F_HIGHDMA;
	dev->vlan_features = dev->features;
	
	dev->hw_features |= NETIF_F_RXALL;  /*Receive errored frames*/
	dev->hw_features |= NETIF_F_RXFCS;  /* RX checksum*/
	
	/* tp zeroed and aligned in alloc_etherdev */
	tp = netdev_priv(dev);

	/* note: tp->chipset set in rtl8139_init_board */
	tp->drv_flags = board_info[ent->driver_data].hw_flags;
	tp->mmio_addr = ioaddr;
	tp->msg_enable =
		(debug < 0 ? RTL8139_DEF_MSG_ENABLE : ((1 << debug) - 1));	
	spin_lock_init (&tp->lock);
	spin_lock_init (&tp->rx_lock);
	INIT_DELAYED_WORK(&tp->thread, rtl8139_thread); /*Init delayed work*/
	tp->mii.dev = dev;
	tp->mii.mdio_read = mdio_read;
	tp->mii.mdio_write = mdio_write;

	tp->mii.phy_id_mask = 0x3f;  //6 bit Phy : This to drive, this is considered to have multiple PHY SMI bus. 
	tp->mii.reg_num_mask = 0x1f; //5 bit RA	
	/* dev is fully set up and ready to use now */
	pr_debug("about to register device named %s (%p)...\n",
		 dev->name, dev);
	/* register a network device  :  Take a completed network device structure and add it to the kernel interfaces.  
	A NETDEV_REGISTER message is sent to the netdev notifier chain.0 is returned on success. A negative  errno code 
        is returned on a failure to set up the device, or if the name is a duplicate.This is a wrapper around 
	register_netdevice that takes the rtnl semaphore and expands the device name if you passed a format string to 
	alloc_netdev. 
	*/
	i = register_netdev (dev);
	if (i) 
		goto err_out;
	/* Similar to the helpers above, these manipulate per-pci_dev
 	* driver-specific data.  They are really just a wrapper around
 	* the generic device structure functions of these calls.
 	*/
	pci_set_drvdata (pdev, dev);
	netdev_info(dev, "%s at 0x%p, %pM, IRQ %d\n",
		    board_info[ent->driver_data].name,
		    ioaddr, dev->dev_addr, pdev->irq);
	netdev_dbg(dev, "Identified 8139 chip type '%s'\n",
		   rtl_chip_info[tp->chipset].name);
	
	/* Find the connected MII xcvrs.
	   Doing this in open() would allow detecting external xcvrs later, but
	   takes too much time. */
#ifdef CONFIG_8139TOO_8129
	if (tp->drv_flags & HAS_MII_XCVR) {
		int phy, phy_idx = 0;
		for (phy = 0; phy < 32 && phy_idx < sizeof(tp->phys); phy++) {
			int mii_status = mdio_read(dev, phy, 1);
			if (mii_status != 0xffff  &&  mii_status != 0x0000) {
				u16 advertising = mdio_read(dev, phy, 4);
				tp->phys[phy_idx++] = phy;
				netdev_info(dev, "MII transceiver %d status 0x%04x advertising %04x\n",
					    phy, mii_status, advertising);
			}
		}
		if (phy_idx == 0) {
			netdev_info(dev, "No MII transceivers found! Assuming SYM transceiver\n");
			tp->phys[0] = 32;
		}	
		
	}else
#endif
	tp->phys[0] = 32;
	tp->mii.phy_id = tp->phys[0];
	/* The lower four bits are the media type. */
	option = (board_idx >= MAX_UNITS) ? 0 : media[board_idx];
	if (option > 0) {
		tp->mii.full_duplex = (option & 0x210) ? 1 : 0; /*  Basic Mode Control Register  p.g : 34*/
		tp->default_port = option & 0xFF;
		if (tp->default_port)
			tp->mii.force_media = 1; /* is autoneg. disabled? */
	}
	if (board_idx < MAX_UNITS  &&  full_duplex[board_idx] > 0)
		tp->mii.full_duplex = full_duplex[board_idx];
	if (tp->mii.full_duplex) {
		netdev_info(dev, "Media type forced to Full Duplex\n");
		/* Changing the MII-advertised media because might prevent
		   re-connection. */
		tp->mii.force_media = 1;
	}
	if (tp->default_port) {
		netdev_info(dev, "  Forcing %dMbps %s-duplex operation\n",
			    (option & 0x20 ? 100 : 10),
			    (option & 0x10 ? "full" : "half")); /*  Basic Mode Control Register : full : bit 8 bit ,100 : 13 bit page : 63 -refer RTL8139CL+ manual */ 
		mdio_write(dev, tp->phys[0], 0,
				   ((option & 0x20) ? 0x2000 : 0) | 	/* 100Mbps? */
				   ((option & 0x10) ? 0x0100 : 0)); /* Full duplex? */
	}
	/* Put the chip into low-power mode. */
	if (rtl_chip_info[tp->chipset].flags & HasHltClk)
		RTL_W8 (HltClk, 'H');	/* 'R' would leave the clock running. */
		
	return 0;
err_out:
	netif_napi_del(&tp->napi); /* remove a napi context */
	__rtl8139_cleanup_dev (dev);
	pci_disable_device (pdev);
	return i;
}
static void rtl8139_remove_one(struct pci_dev *dev)
{
	pr_info("%s: device removed safly\n",__func__);
}
/* Serial EEPROM section. */
/* 93C46 Command Register :
	Bit 7-6, R/W EEM1-0  , 1 0 : 93C46 programming: In this mode, both network and host bus master operations are disabled. The 93C46 can be directly accessed via bit3-0 
	which now reflect the states of EECS, EESK, EEDI, & EEDO pins respectively.
	EE_CS  		  : Bit 3 W/R EECS  
	EE_SHIFT_CLK      : Bit 2 W/R EESK
	EE_DATA_WRITE     : Bit 1 W/R EEDI : EEPROM chip data in
	EE_DATA_READ      : Bit 0 W/R EEDO : EEPROM chip data out 
 */
/*  EEPROM_Ctrl bits. */
#define EE_SHIFT_CLK	0x04	/* EEPROM shift clock. */
#define EE_CS		0x08	/* EEPROM chip select. */
#define EE_DATA_WRITE	0x02	/* EEPROM chip data in. */
#define EE_WRITE_0	0x00
#define EE_WRITE_1	0x02
#define EE_DATA_READ	0x01	/* EEPROM chip data out. */
#define EE_ENB	(0x80 | EE_CS)  /* EECS | Operating Mode (EEM1-0) i.e 93C46 programming:*/
/* Delay between EEPROM clock transitions.
   No extra delay is needed with 33Mhz PCI, but 66Mhz may change this.
 */
#define eeprom_delay()	(void)RTL_R8(Cfg9346)

/* The EEPROM commands include the alway-set leading bit. */
#define EE_WRITE_CMD	(5)
#define EE_READ_CMD	(6)
#define EE_ERASE_CMD	(7)

static int read_eeprom(void __iomem *ioaddr, int location, int addr_len)
{
	int i;
	unsigned retval = 0;
	int read_cmd = location | (EE_READ_CMD << addr_len);
	/* Shift the read command bits out. */
	for (i = 4 + addr_len; i >= 0; i--) {
		int dataval = (read_cmd & (1 << i)) ? EE_DATA_WRITE : 0;
		RTL_W8 (Cfg9346, EE_ENB | dataval);
		eeprom_delay ();
		RTL_W8 (Cfg9346, EE_ENB | dataval | EE_SHIFT_CLK);
		eeprom_delay ();
	}
	RTL_W8 (Cfg9346, EE_ENB);
	eeprom_delay ();

	for (i = 16; i > 0; i--) {
		RTL_W8 (Cfg9346, EE_ENB | EE_SHIFT_CLK);
		eeprom_delay ();
		retval =
		    (retval << 1) | ((RTL_R8 (Cfg9346) & EE_DATA_READ) ? 1 :
				     0);
		RTL_W8 (Cfg9346, EE_ENB);
		eeprom_delay ();
	}

	/* Terminate the EEPROM access. */
	RTL_W8(Cfg9346, 0);
	eeprom_delay ();

	return retval;

}

/**
 *	napi_enable - enable NAPI scheduling
 *	@n: NAPI context
 *
 * Resume NAPI from being scheduled on this context.
 * Must be paired with napi_disable.
 */

/**
 *	netif_start_queue - allow transmit
 *	@dev: network device
 *
 *	Allow upper layers to call the device hard_start_xmit routine.
 */

static int rtl8139_open (struct net_device *dev)
{
	struct rtl8139_private *tp = netdev_priv(dev);
	void __iomem *ioaddr = tp->mmio_addr;
	const int irq = tp->pci_dev->irq;
	int retval;
	retval = request_irq(irq, rtl8139_interrupt, IRQF_SHARED, dev->name, dev);
	if (retval)
		return retval;
	/* dma allocation for rx and tx buffer*/
	tp->tx_bufs = dma_alloc_coherent(&tp->pci_dev->dev, TX_BUF_TOT_LEN,
					   &tp->tx_bufs_dma, GFP_KERNEL);
	tp->rx_ring = dma_alloc_coherent(&tp->pci_dev->dev, RX_BUF_TOT_LEN,
					   &tp->rx_ring_dma, GFP_KERNEL);
	if (tp->tx_bufs == NULL || tp->rx_ring == NULL) {
		free_irq(irq, dev);	
	
		if (tp->tx_bufs)
			dma_free_coherent(&tp->pci_dev->dev, TX_BUF_TOT_LEN,
					    tp->tx_bufs, tp->tx_bufs_dma);
		if (tp->rx_ring)
			dma_free_coherent(&tp->pci_dev->dev, RX_BUF_TOT_LEN,
					    tp->rx_ring, tp->rx_ring_dma);
		return -ENOMEM;
	}
	napi_enable(&tp->napi);  /*enable NAPI scheduling*/
	tp->mii.full_duplex = tp->mii.force_media;     /* autoneg mode*/
	tp->tx_flag = (TX_FIFO_THRESH << 11) & 0x003f0000;   /*80000 & 0x003f0000 i.e ERTXTH0 to 5 six bit : i.e 3f */
	rtl8139_init_ring (dev);
	rtl8139_hw_start (dev);
	netif_start_queue (dev);

	netif_dbg(tp, ifup, dev,
		  "%s() ioaddr %#llx IRQ %d GP Pins %02x %s-duplex\n",
		  __func__,
		  (unsigned long long)pci_resource_start (tp->pci_dev, 1),
		  irq, RTL_R8 (MediaStatus),
		  tp->mii.full_duplex ? "full" : "half");

	rtl8139_start_thread(tp);

	return 0;
	
}
/**
 * mii_check_media - check the MII interface for a carrier/speed/duplex change
 * @mii: the MII interface
 * @ok_to_print: OK to print link up/down messages
 * @init_media: OK to save duplex mode in @mii
 *
 * Returns 1 if the duplex mode changed, 0 if not.
 * If the media type is forced, always returns 0.
 */
static void rtl_check_media (struct net_device *dev, unsigned int init_media)
{
	struct rtl8139_private *tp = netdev_priv(dev);

	if (tp->phys[0] >= 0) {
		mii_check_media(&tp->mii, netif_msg_link(tp), init_media);
	}
}
static void rtl8139_hw_start (struct net_device *dev)
{
	struct rtl8139_private *tp = netdev_priv(dev);
	void __iomem *ioaddr = tp->mmio_addr;
	u32 i;
	u8 tmp;
	/* Bring old chips out of low-power mode. */
	if (rtl_chip_info[tp->chipset].flags & HasHltClk)
		RTL_W8 (HltClk, 'R');
	rtl8139_chip_reset (ioaddr);	
	/* unlock Config[01234] and BMCR register writes */
	RTL_W8_F (Cfg9346, Cfg9346_Unlock);
	/* Restore our idea of the MAC address. */
	RTL_W32_F (MAC0 + 0, le32_to_cpu (*(__le32 *) (dev->dev_addr + 0)));
	RTL_W32_F (MAC0 + 4, le16_to_cpu (*(__le16 *) (dev->dev_addr + 4)));

	tp->cur_rx = 0;
	/* init Rx ring buffer DMA address */
	RTL_W32_F (RxBuf, tp->rx_ring_dma);

	/* Must enable Tx/Rx before setting transfer thresholds! */
	RTL_W8 (ChipCmd, CmdRxEnb | CmdTxEnb);
	
	tp->rx_config = rtl8139_rx_config | AcceptBroadcast | AcceptMyPhys;
	RTL_W32 (RxConfig, tp->rx_config);
	RTL_W32 (TxConfig, rtl8139_tx_config);
	
	rtl_check_media (dev, 1); //TODO
	if (tp->chipset >= CH_8139B) {
		/* Disable magic packet scanning, which is enabled
		 * when PM is enabled in Config1.  It can be reenabled
		 * via ETHTOOL_SWOL if desired.  */
		RTL_W8 (Config3, RTL_R8 (Config3) & ~Cfg3_Magic);
	}
	netdev_dbg(dev, "init buffer addresses\n");
	/* Lock Config[01234] and BMCR register writes */
	RTL_W8 (Cfg9346, Cfg9346_Lock);

	/* init Tx buffer DMA addresses */
	for (i = 0; i < NUM_TX_DESC; i++)
		RTL_W32_F (TxAddr0 + (i * 4), tp->tx_bufs_dma + (tp->tx_buf[i] - tp->tx_bufs));
	RTL_W32 (RxMissed, 0);

	rtl8139_set_rx_mode (dev);  //TODO
	/* no early-rx interrupts */
	RTL_W16 (MultiIntr, RTL_R16 (MultiIntr) & MultiIntrClear);
	/* make sure RxTx has started */
	tmp = RTL_R8 (ChipCmd);
	if ((!(tmp & CmdRxEnb)) || (!(tmp & CmdTxEnb)))
		RTL_W8 (ChipCmd, CmdRxEnb | CmdTxEnb);

	/* Enable all known interrupts by setting the interrupt mask. */
	RTL_W16 (IntrMask, rtl8139_intr_mask);
	
}
/* Initialize the Rx and Tx rings, along with various 'dev' bits. */
static void rtl8139_init_ring (struct net_device *dev)
{
	struct rtl8139_private *tp = netdev_priv(dev);
	int i;

	tp->cur_rx = 0;
	tp->cur_tx = 0;
	tp->dirty_tx = 0;

	for (i = 0; i < NUM_TX_DESC; i++)
		tp->tx_buf[i] = &tp->tx_bufs[i * TX_BUF_SIZE];
}
/*TODO : All of this is magic and undocumented.*/
/* This must be global for CONFIG_8139TOO_TUNE_TWISTER case */
static int next_tick = 3 * HZ;

#ifndef CONFIG_8139TOO_TUNE_TWISTER
static inline void rtl8139_tune_twister (struct net_device *dev,
				  struct rtl8139_private *tp) {}
#else
enum TwisterParamVals {
	PARA78_default	= 0x78fa8388,
	PARA7c_default	= 0xcb38de43,	/* param[0][3] */
	PARA7c_xxx	= 0xcb38de43,
};

static const unsigned long param[4][4] = {
	{0xcb39de43, 0xcb39ce43, 0xfb38de03, 0xcb38de43},
	{0xcb39de43, 0xcb39ce43, 0xcb39ce83, 0xcb39ce83},
	{0xcb39de43, 0xcb39ce43, 0xcb39ce83, 0xcb39ce83},
	{0xbb39de43, 0xbb39ce43, 0xbb39ce83, 0xbb39ce83}
};
static void rtl8139_tune_twister (struct net_device *dev,
				  struct rtl8139_private *tp)
{
	int linkcase;
	void __iomem *ioaddr = tp->mmio_addr;

	/* This is a complicated state machine to configure the "twister" for
	   impedance/echos based on the cable length.
	   All of this is magic and undocumented.
	 */
	switch (tp->twistie) {
		case 1:
		if (RTL_R16 (CSCR) & CSCR_LinkOKBit) {
			/* We have link beat, let us tune the twister. */
			RTL_W16 (CSCR, CSCR_LinkDownOffCmd);
			tp->twistie = 2;	/* Change to state 2. */
			next_tick = HZ / 10;
		} else {
			/* Just put in some reasonable defaults for when beat returns. */
			RTL_W16 (CSCR, CSCR_LinkDownCmd);
			RTL_W32 (FIFOTMS, 0x20);	/* Turn on cable test mode. */
			RTL_W32 (PARA78, PARA78_default);
			RTL_W32 (PARA7c, PARA7c_default);
			tp->twistie = 0;	/* Bail from future actions. */
		}
		break;
	case 2:
		/* Read how long it took to hear the echo. */
		linkcase = RTL_R16 (CSCR) & CSCR_LinkStatusBits;
		if (linkcase == 0x7000)
			tp->twist_row = 3;
		else if (linkcase == 0x3000)
			tp->twist_row = 2;
		else if (linkcase == 0x1000)
			tp->twist_row = 1;
		else
			tp->twist_row = 0;
		tp->twist_col = 0;
		tp->twistie = 3;	/* Change to state 2. */
		next_tick = HZ / 10;
		break;
	case 3:
		/* Put out four tuning parameters, one per 100msec. */
		if (tp->twist_col == 0)
			RTL_W16 (FIFOTMS, 0);
		RTL_W32 (PARA7c, param[(int) tp->twist_row]
			 [(int) tp->twist_col]);
		next_tick = HZ / 10;
		if (++tp->twist_col >= 4) {
			/* For short cables we are done.
			   For long cables (row == 3) check for mistune. */
			tp->twistie =
			    (tp->twist_row == 3) ? 4 : 0;
		}
		break;
	case 4:
		/* Special case for long cables: check for mistune. */
		if ((RTL_R16 (CSCR) &
		     CSCR_LinkStatusBits) == 0x7000) {
			tp->twistie = 0;
			break;
		} else {
			RTL_W32 (PARA7c, 0xfb38de03);
			tp->twistie = 5;
			next_tick = HZ / 10;
		}
		break;
	case 5:
		/* Retune for shorter cable (column 2). */
		RTL_W32 (FIFOTMS, 0x20);
		RTL_W32 (PARA78, PARA78_default);
		RTL_W32 (PARA7c, PARA7c_default);
		RTL_W32 (FIFOTMS, 0x00);
		tp->twist_row = 2;
		tp->twist_col = 0;
		tp->twistie = 3;
		next_tick = HZ / 10;
		break;

	default:
		/* do nothing */
		break;
	}
}
#endif /* CONFIG_8139TOO_TUNE_TWISTER */

/**
 * struct mdio_if_info - Ethernet controller MDIO interface
 * @prtad: PRTAD of the PHY (%MDIO_PRTAD_NONE if not present/unknown)
 * @mmds: Mask of MMDs expected to be present in the PHY.  This must be
 *	non-zero unless @prtad = %MDIO_PRTAD_NONE.
 * @mode_support: MDIO modes supported.  If %MDIO_SUPPORTS_C22 is set then
 *	MII register access will be passed through with @devad =
 *	%MDIO_DEVAD_NONE.  If %MDIO_EMULATE_C22 is set then access to
 *	commonly used clause 22 registers will be translated into
 *	clause 45 registers.
 * @dev: Net device structure
 * @mdio_read: Register read function; returns value or negative error code
 * @mdio_write: Register write function; returns 0 or negative error code
 */
 /*  mdio_read (@param1 : the net device to read  , @param2 :      the phy address to read  ,@param3:      the phy regiester id to read  )
  *  Read MII registers through MDIO and MDC using MDIO management frame structure and protocol(defined by ISO/IEC).
  *  The Auto-Negotiation Link Partner Ability Register (ANLPAR) at address 0x05h is used to receive the base link code word as well as all next page code words during the negotiation.
 */
/* This is a complicated state machine to configure the "twister" for impedance/echos based on the cable length. All of this is magic and undocumented. */ 

static inline void rtl8139_thread_iter (struct net_device *dev,
				 struct rtl8139_private *tp,
				 void __iomem *ioaddr)
{
	int mii_lpa;
	mii_lpa = mdio_read (dev, tp->phys[0], MII_LPA);                     /*   read MII PHY register : --MII_LPA :Link partner ability reg  */	
	if (!tp->mii.force_media && mii_lpa != 0xffff) {                     /*    force_media: is autoneg.??  # Basic Mode Control Register :- it should set all for autoneg */
		
		int duplex = ((mii_lpa & LPA_100FULL) ||                     /*     LPA_100FULL : Can do 100mbps full-duplex */
			      (mii_lpa & 0x01C0) == 0x0040);                 /*     Auto-Negotiation Link Partner Ability Register -Offset 0068h-0069h : 
										    Checkingh whether "10Base-T full duplex is supported by link partner" */	
		if (tp->mii.full_duplex != duplex) {
			tp->mii.full_duplex = duplex;
			if (mii_lpa) {
				netdev_info(dev, "Setting %s-duplex based on MII #%d link partner ability of %04x\n",
					    tp->mii.full_duplex ? "full" : "half",
					    tp->phys[0], mii_lpa);
			} else {
				netdev_info(dev, "media is unconnected, link down, or incompatible connection\n");
			}
#if 0
			RTL_W8 (Cfg9346, Cfg9346_Unlock);
			RTL_W8 (Config1, tp->mii.full_duplex ? 0x60 : 0x20);
			RTL_W8 (Cfg9346, Cfg9346_Lock);
#endif	
		} 
	}
	next_tick = HZ * 60;
	rtl8139_tune_twister (dev, tp);
	
	
	netdev_dbg(dev, "Media selection tick, Link partner %04x\n",
		   RTL_R16(NWayLPAR));
	netdev_dbg(dev, "Other registers are IntMask %04x IntStatus %04x\n",
		   RTL_R16(IntrMask), RTL_R16(IntrStatus));
	netdev_dbg(dev, "Chip config %02x %02x\n",
		   RTL_R8(Config0), RTL_R8(Config1));
}
/* RTNL is used as a global lock for all changes to network configuration  */
/**
 *	netif_running - test if up
 *	@dev: network device
 *
 *	Test if the device has been brought up.
 */
static void rtl8139_thread (struct work_struct *work)
{
	struct rtl8139_private *tp =
		container_of(work, struct rtl8139_private, thread.work);
	struct net_device *dev = tp->mii.dev;
	unsigned long thr_delay = next_tick;
	rtnl_lock();

	if (!netif_running(dev))
		goto out_unlock;

	if (tp->watchdog_fired) {
		tp->watchdog_fired = 0;
		rtl8139_tx_timeout_task(work);
	} else
		rtl8139_thread_iter(dev, tp, tp->mmio_addr);   //TODO: 

	if (tp->have_thread)
		schedule_delayed_work(&tp->thread, thr_delay);
out_unlock:
	rtnl_unlock ();
}

/**
 * schedule_delayed_work - put work task in global workqueue after delay
 * @dwork: job to be done
 * @delay: number of jiffies to wait or 0 for immediate execution
 *
 * After waiting for a given time this puts a job in the kernel-global
 * workqueue.
 */

/*
	If a bottom half shares data with usercontext,youhave two problems.Firstly, thecurrent usercontext can be interrupted by a bottomhalf,andsecondly, thecritical region could been tered from another CPU.Thisis wherespin_lock_bh()(include/linux/spinlock.h) is used.It disables bottom halveson thatCPU,then grabs the lock.spin_unlock_bh() does the reverse.This work sperfectly for UP as well:the spin lock vanishes, and this macro simply be comes local_bh_disable()(include/asm/softirq.h), which protects you from the bottom half being run.
*/
static void rtl8139_start_thread(struct rtl8139_private *tp)
{
	tp->twistie = 0;
	if (tp->chipset == CH_8139_K)
		tp->twistie = 1;
	else if (tp->drv_flags & HAS_LNK_CHNG)
		return;

	tp->have_thread = 1;
	tp->watchdog_fired = 0;

	schedule_delayed_work(&tp->thread, next_tick);
}


static inline void rtl8139_tx_clear (struct rtl8139_private *tp)
{
	tp->cur_tx = 0;
	tp->dirty_tx = 0;

	/* XXX account for unsent Tx packets in tp->stats.tx_dropped */
}
static void rtl8139_tx_timeout_task (struct work_struct *work)
{
	struct rtl8139_private *tp =
		container_of(work, struct rtl8139_private, thread.work);
	struct net_device *dev = tp->mii.dev;
	void __iomem *ioaddr = tp->mmio_addr;
	int i;
	u8 tmp8;
	
	netdev_dbg(dev, "Transmit timeout, status %02x %04x %04x media %02x\n",
		   RTL_R8(ChipCmd), RTL_R16(IntrStatus),
		   RTL_R16(IntrMask), RTL_R8(MediaStatus));
	/* Emit info to figure out what went wrong. */
	etdev_dbg(dev, "Tx queue start entry %ld  dirty entry %ld\n",
		   tp->cur_tx, tp->dirty_tx);
	for (i = 0; i < NUM_TX_DESC; i++)
		netdev_dbg(dev, "Tx descriptor %d is %08x%s\n",
			   i, RTL_R32(TxStatus0 + (i * 4)),
			   i == tp->dirty_tx % NUM_TX_DESC ?
			   " (queue head)" : "");
	tp->xstats.tx_timeouts++;
	
	/* disable Tx ASAP, if not already */
	tmp8 = RTL_R8 (ChipCmd);
	if (tmp8 & CmdTxEnb)
		RTL_W8 (ChipCmd, CmdRxEnb);
	/* Locking Between User Context and BHs : */
	spin_lock_bh(&tp->rx_lock);
	/* Disable interrupts by clearing the interrupt mask. */
	RTL_W16 (IntrMask, 0x0000);
	/* Stop a shared interrupt from scavenging while we are. */
	spin_lock_irq(&tp->lock);
	rtl8139_tx_clear (tp);
	spin_unlock_irq(&tp->lock);

	/* ...and finally, reset everything */
	if (netif_running(dev)) {
		rtl8139_hw_start (dev);
		netif_wake_queue (dev);
	}
	spin_unlock_bh(&tp->rx_lock);
}

static void rtl8139_tx_timeout (struct net_device *dev)
{
	struct rtl8139_private *tp = netdev_priv(dev);
	tp->watchdog_fired = 1;
	if (!tp->have_thread) {
		/* initialize all of a work item in one go */
		INIT_DELAYED_WORK(&tp->thread, rtl8139_thread);
		schedule_delayed_work(&tp->thread, next_tick);  /* put work task in global workqueue after delay */
	}
}
static netdev_tx_t rtl8139_start_xmit (struct sk_buff *skb,
					     struct net_device *dev)
{
	struct rtl8139_private *tp = netdev_priv(dev);
	void __iomem *ioaddr = tp->mmio_addr;
	unsigned int entry;
	unsigned int len = skb->len;
	unsigned long flags;
	
	/* Calculate the next Tx descriptor entry. */
	
	entry = tp->cur_tx % NUM_TX_DESC;
	
	/* Note: the chip doesn't have auto-pad! */
	if (likely(len < TX_BUF_SIZE)) {
		if (len < ETH_ZLEN)
			memset(tp->tx_buf[entry], 0, ETH_ZLEN);
		skb_copy_and_csum_dev(skb, tp->tx_buf[entry]);
		/*dev_kfree_skb_irq(skb) : when caller drops a packet from irq context, replacing kfree_skb(skb) */
		dev_kfree_skb_any(skb);
	}else {
		dev_kfree_skb_any(skb);
		dev->stats.tx_dropped++;
		return NETDEV_TX_OK;
	}
	spin_lock_irqsave(&tp->lock, flags);
	/*
	 * Writing to TxStatus triggers a DMA transfer of the data
	 * copied to tp->tx_buf[entry] above. Use a memory barrier
	 * to make sure that the device sees the updated data.
	 */
	wmb();
	RTL_W32_F (TxStatus0 + (entry * sizeof (u32)),
		   tp->tx_flag | max(len, (unsigned int)ETH_ZLEN));

	tp->cur_tx++;
	if ((tp->cur_tx - NUM_TX_DESC) == tp->dirty_tx)
		netif_stop_queue (dev);
	spin_unlock_irqrestore(&tp->lock, flags);
	
	netif_dbg(tp, tx_queued, dev, "Queued Tx packet size %u to slot %d\n",
		  len, entry);

	return NETDEV_TX_OK; /* driver took care of packet */	
}
/*
mb()
	A full system memory barrier. All memory operations before the mb() in the instruction stream will be committed before any operations after the mb() are committed. This ordering 	 will be visible to all bus masters in the system. It will also ensure the order in which accesses from a single processor reaches slave devices.
rmb()
	Like mb(), but only guarantees ordering between read accesses. That is, all read operations before an rmb() will be committed before any read operations after the rmb().
wmb()
	Like mb(), but only guarantees ordering between write accesses. That is, all write operations before a wmb() will be committed before any write operations after the wmb().
*/

/**
 *	netif_wake_queue - restart transmit
 *	@dev: network device
 *
 *	Allow upper layers to call the device hard_start_xmit routine.
 *	Used for flow control when transmit resources are available.
 */

static void rtl8139_tx_interrupt (struct net_device *dev,
				  struct rtl8139_private *tp,
				  void __iomem *ioaddr)
{
	unsigned long dirty_tx, tx_left;
	assert (dev != NULL);
	assert (ioaddr != NULL);

	dirty_tx = tp->dirty_tx;
	tx_left = tp->cur_tx - dirty_tx;
	while (tx_left > 0) {
		int entry = dirty_tx % NUM_TX_DESC;
		int txstatus;
		txstatus = RTL_R32 (TxStatus0 + (entry * sizeof (u32)));  /* TxStatus0 + entry(0-3) * discriptor size*/
		if (!(txstatus & (TxStatOK | TxUnderrun | TxAborted)))
			break;	/* It still hasn't been Txed */
		/* Note: TxCarrierLost is always asserted at 100mbps. */
		if (txstatus & (TxOutOfWindow | TxAborted)) {
			/* There was an major error, log it. */
			netif_dbg(tp, tx_err, dev, "Transmit error, Tx status %08x\n",
				  txstatus);
			dev->stats.tx_errors++;
			if (txstatus & TxAborted) {
				dev->stats.tx_aborted_errors++;
				RTL_W32 (TxConfig, TxClearAbt);
				RTL_W16 (IntrStatus, TxErr);
				wmb();                                    /*The wmb() macro does: prevent reordering of the stores. */
			}
			if (txstatus & TxCarrierLost)
				dev->stats.tx_carrier_errors++;
			if (txstatus & TxOutOfWindow)
				dev->stats.tx_window_errors++;
		} else {
			if (txstatus & TxUnderrun) {
				/* Add 64 to the Tx FIFO threshold. */
				if (tp->tx_flag < 0x00300000)      /*if Collision Count*/
					tp->tx_flag += 0x00020000; /*Threshold level in the Tx FIFO is increased*/
				dev->stats.tx_fifo_errors++;
			}
			dev->stats.collisions += (txstatus >> 24) & 15;  /*right shit 24 times i.e Number of Collision Count & (0001(collision signal)0101 (Collision Count) ) i.e 15*/
			u64_stats_update_begin(&tp->tx_stats.syncp);     /*seq count of packet lock*/ 
			tp->tx_stats.packets++;				 /*packets update */
			tp->tx_stats.bytes += txstatus & 0x7ff;          /*0-11 bits for The total size in bytes of the data in this descriptor*/
			u64_stats_update_end(&tp->tx_stats.syncp);       /* seq count of packet unlock*/
		}
		dirty_tx++;
		tx_left--;
	}
#ifndef RTL8139_NDEBUG
	if (tp->cur_tx - dirty_tx > NUM_TX_DESC) {
		netdev_err(dev, "Out-of-sync dirty pointer, %ld vs. %ld\n",
			   dirty_tx, tp->cur_tx);
		dirty_tx += NUM_TX_DESC;
	}
#endif /* RTL8139_NDEBUG */
	/* only wake the queue if we did work, and the queue is stopped */
	if (tp->dirty_tx != dirty_tx) {
		tp->dirty_tx = dirty_tx;
		mb();
		netif_wake_queue (dev); /* restart transmit */
	} 
}

#if RX_BUF_IDX == 3
static inline void wrap_copy(struct sk_buff *skb, const unsigned char *ring,
				 u32 offset, unsigned int size)
{
	u32 left = RX_BUF_LEN - offset;

	if (size > left) {
		skb_copy_to_linear_data(skb, ring + offset, left);
		skb_copy_to_linear_data_offset(skb, left, ring, size - left);
	} else
		skb_copy_to_linear_data(skb, ring + offset, size);
}
#endif
static void rtl8139_isr_ack(struct rtl8139_private *tp)
{
	void __iomem *ioaddr = tp->mmio_addr;
	u16 status;
	
	status = RTL_R16 (IntrStatus) & RxAckBits;
	/* Clear out errors and receive interrupts */
	if (likely(status != 0)) {
		if (unlikely(status & (RxFIFOOver | RxOverflow))) {
			tp->dev->stats.rx_errors++;
			if (status & RxFIFOOver)
				tp->dev->stats.rx_fifo_errors++;
		}
		RTL_W16_F (IntrStatus, RxAckBits);
	}
}
/**
 *	__napi_alloc_skb - allocate skbuff for rx in a specific NAPI instance
 *	@napi: napi instance this buffer was allocated for
 *	@len: length to allocate
 *	@gfp_mask: get_free_pages mask, passed to alloc_skb and alloc_pages
 *
 *	Allocate a new sk_buff for use in NAPI receive.  This buffer will
 *	attempt to allocate the head from a special reserved region used
 *	only for NAPI Rx allocation.  By doing this we can save several
 *	CPU cycles by avoiding having to disable and re-enable IRQs.
 *
 *	%NULL is returned if there is no free memory.
 */
/**
 *	skb_put - add data to a buffer
 *	@skb: buffer to use
 *	@len: amount of data to add
 *
 *	This function extends the used data area of the buffer. If this would
 *	exceed the total buffer size the kernel will panic. A pointer to the
 *	first byte of the extra data is returned.
 */
/**
 * eth_type_trans - determine the packet's protocol ID.
 * @skb: received socket data
 * @dev: receiving network device
 *
 * The rule here is that we
 * assume 802.3 if the type field is short enough to be a length.
 * This is normal practice and works for any 'now in use' protocol.
 */
 /**
 *	netif_receive_skb - process receive buffer from network
 *	@skb: buffer to process
 *
 *	netif_receive_skb() is the main receive data processing function.
 *	It always succeeds. The buffer may be dropped during processing
 *	for congestion control or by the protocol layers.
 *
 *	This function may only be called from softirq context and interrupts
 *	should be enabled.
 *
 *	Return values (usually ignored):
 *	NET_RX_SUCCESS: no congestion
 *	NET_RX_DROP: packet was dropped
 */
static int rtl8139_rx(struct net_device *dev, struct rtl8139_private *tp,
		      int budget)
{
	void __iomem *ioaddr = tp->mmio_addr;
	int received = 0;
	unsigned char *rx_ring = tp->rx_ring;
	unsigned int cur_rx = tp->cur_rx;
	unsigned int rx_size = 0;
	netdev_dbg(dev, "In %s(), current %04x BufAddr %04x, free to %04x, Cmd %02x\n",
		   __func__, (u16)cur_rx,
		   RTL_R16(RxBufAddr), RTL_R16(RxBufPtr), RTL_R8(ChipCmd));
	while (netif_running(dev) && received < budget &&
	       (RTL_R8 (ChipCmd) & RxBufEmpty) == 0) {
		u32 ring_offset = cur_rx % RX_BUF_LEN;
		u32 rx_status;
		unsigned int pkt_size;
		struct sk_buff *skb;
		
		rmb();
		/* read size+status of next frame from DMA ring buffer */
		rx_status = le32_to_cpu (*(__le32 *) (rx_ring + ring_offset));
		rx_size = rx_status >> 16;              /* status : First 16 bit */
		if (likely(!(dev->features & NETIF_F_RXFCS)))       /*Axpend FCS to skb pkt data */
			pkt_size = rx_size - 4;  /* first two bytes are receive status register*/
		else 
			pkt_size = rx_size;
		netif_dbg(tp, rx_status, dev, "%s() status %04x, size %04x, cur %04x\n",
			  __func__, rx_status, rx_size, cur_rx);	
#if RTL8139_DEBUG > 2
		print_hex_dump(KERN_DEBUG, "Frame contents: ",
			       DUMP_PREFIX_OFFSET, 16, 1,
			       &rx_ring[ring_offset], 70, true);
#endif
		
		/* Packet copy from FIFO still in progress.
		 * Theoretically, this should never happen
		 * since EarlyRx is disabled.
		 */	
		 if (unlikely(rx_size == 0xfff0)) {
			if (!tp->fifo_copy_timeout)
				tp->fifo_copy_timeout = jiffies + 2;
 		/* time_after(a,b) returns true if the time a is after time b.*/
			else if (time_after(jiffies, tp->fifo_copy_timeout)) {
				netdev_dbg(dev, "hung FIFO. Reset\n");
				rx_size = 0;
				goto no_early_rx;
			}
		 netif_dbg(tp, intr, dev, "fifo copy in progress\n");
			tp->xstats.early_rx++;
			break;
		 }
no_early_rx:
		tp->fifo_copy_timeout = 0;
		 /* If Rx err or invalid rx_size/rx_status received
		 * (which happens if we get lost in the ring),
		 * Rx process gets reset, so we abort any further
		 * Rx processing.
		 */	
		if (unlikely((rx_size > (MAX_ETH_FRAME_SIZE+4)) ||
			     (rx_size < 8) ||
			     (!(rx_status & RxStatusOK)))) {
			if ((dev->features & NETIF_F_RXALL) &&              /* Receive errored frames too */
			    (rx_size <= (MAX_ETH_FRAME_SIZE + 4)) &&
			    (rx_size >= 8) &&
			    (!(rx_status & RxStatusOK))) {
				/* Length is at least mostly OK, but pkt has
				 * error.  I'm hoping we can handle some of these
				 * errors without resetting the chip. --Ben
				 */
				dev->stats.rx_errors++;
				if (rx_status & RxCRCErr) {
					dev->stats.rx_crc_errors++;
					goto keep_pkt;
				}
				if (rx_status & RxRunt) {
					dev->stats.rx_length_errors++;
					goto keep_pkt;
				}
		        }
			rtl8139_rx_err (rx_status, dev, tp, ioaddr); //TODO
			received = -1;
			goto out;				
		}
keep_pkt:
		/* Malloc up new buffer, compatible with net-2e. */
		/* Omit the four octet CRC from the length. */
		skb = napi_alloc_skb(&tp->napi, pkt_size);
		if (likely(skb)) {
#if RX_BUF_IDX == 3
			wrap_copy(skb, rx_ring, ring_offset+4, pkt_size);
#else
			skb_copy_to_linear_data (skb, &rx_ring[ring_offset + 4], pkt_size);  //memcpy(skb->data, from, len);
#endif
			skb_put (skb, pkt_size); /*  add data to a buffer */
			skb->protocol = eth_type_trans (skb, dev);i /*  determine the packet's protocol ID. */
			u64_stats_update_begin(&tp->rx_stats.syncp);  //Perform non atomic operation after 
			tp->rx_stats.packets++;
			tp->rx_stats.bytes += pkt_size;
			u64_stats_update_end(&tp->rx_stats.syncp);
			
			netif_receive_skb (skb);/* process receive buffer from network */	
		}else {
			dev->stats.rx_dropped++;
		}
		received++;
		/* update tp->cur_rx to next writing location  */
		
		cur_rx = (cur_rx + rx_size + 4 + 3) & ~3;              /*  ~3 : First two bytes are receive status register*/ 
		RTL_W16 (RxBufPtr, (u16) (cur_rx - 16));               /* 16 byte align the IP fields  */
		
		rtl8139_isr_ack(tp);
	}
	if (unlikely(!received || rx_size == 0xfff0))
		rtl8139_isr_ack(tp);
	netdev_dbg(dev, "Done %s(), current %04x BufAddr %04x, free to %04x, Cmd %02x\n",
		   __func__, cur_rx,
		   RTL_R16(RxBufAddr), RTL_R16(RxBufPtr), RTL_R8(ChipCmd));
	
	tp->cur_rx = cur_rx;

	/*
	 * The receive buffer should be mostly empty.
	 * Tell NAPI to reenable the Rx irq.
	 */
	if (tp->fifo_copy_timeout)
		received = budget;
out:
	return received;	
}
static void rtl8139_weird_interrupt (struct net_device *dev,
				     struct rtl8139_private *tp,
				     void __iomem *ioaddr,
				     int status, int link_changed)
{
	netdev_dbg(deve "Abnormal interrupt, status %08x\n", status);	
	assert (dev != NULL);
	assert (tp != NULL);
	assert (ioaddr != NULL);
	/* Update the error count. */
	dev->stats.rx_missed_errors += RTL_R32 (RxMissed);
	RTL_W32 (RxMissed, 0);
	if ((status & RxUnderrun) && link_changed &&
	    (tp->drv_flags & HAS_LNK_CHNG)) {
		rtl_check_media(dev, 0);              // TODO :-->
		status &= ~RxUnderrun;
	}
	if (status & (RxUnderrun | RxErr))
		dev->stats.rx_errors++;
	if (status & PCSTimeout)
		dev->stats.rx_length_errors++
	if (status & RxUnderrun)
		dev->stats.rx_fifo_errors++;
	if (status & PCIErr) {
		u16 pci_cmd_status;
		pci_read_config_word (tp->pci_dev, PCI_STATUS, &pci_cmd_status);
		pci_write_config_word (tp->pci_dev, PCI_STATUS, pci_cmd_status);

		netdev_err(dev, "PCI Bus error %04x\n", pci_cmd_status);
	}
}/* close rtl8139_weird_interrupt */
static int rtl8139_poll(struct napi_struct *napi, int budget)
{
	struct rtl8139_private *tp = container_of(napi, struct rtl8139_private, napi);
	struct net_device *dev = tp->dev;
	void __iomem *ioaddr = tp->mmio_addr;
	int work_done;
	spin_lock(&tp->rx_lock);
	work_done = 0;
	if (likely(RTL_R16(IntrStatus) & RxAckBits))  /*if RxIFOOver | RxOverflow | RxOK is set then work_done ++ */
		work_done += rtl8139_rx(dev, tp, budget);
	
	if (work_done < budget) {
		unsigned long flags;
		/*
		 * Order is important since data can get interrupted
		 * again when we think we are done.
		 */
		spin_lock_irqsave(&tp->lock, flags);
		__napi_complete(napi);                   //NAPI processing complete
		RTL_W16_F(IntrMask, rtl8139_intr_mask);
		spin_unlock_irqrestore(&tp->lock, flags);
	}
	spin_unlock(&tp->rx_lock);

	return work_done;
}
/**
 *	napi_schedule_prep - check if napi can be scheduled
 *	@n: napi context
 *
 * Test if NAPI routine is already running, and if not mark
 * it as running.  This is used as a condition variable
 * insure only one NAPI poll instance runs.  We also make
 * sure there is no pending NAPI disable.
 */
/**
 *	napi_schedule - schedule NAPI poll
 *	@n: napi context
 *
 * Schedule NAPI poll routine to be called if it is not already
 * running.
 */
/* The interrupt handler does all of the Rx thread work and cleans up
   after the Tx thread. */
static irqreturn_t rtl8139_interrupt (int irq, void *dev_instance)
{
	struct net_device *dev = (struct net_device *) dev_instance;
	struct rtl8139_private *tp = netdev_priv(dev);
	void __iomem *ioaddr = tp->mmio_addr;
	u16 status, ackstat;
	int link_changed = 0; /* avoid bogus "uninit" warning */
	int handled = 0;
	spin_lock (&tp->lock);
	status = RTL_R16 (IntrStatus);
	/* shared irq? */
	if (unlikely((status & rtl8139_intr_mask) == 0))
		goto out;
	ndled = 1;	
	/* h/w no longer present (hotplug?) or major error, bail */
	if (unlikely(status == 0xFFFF))
		goto out;
	/* close possible race's with dev_close netif_running - test if up*/
	if (unlikely(!netif_running(dev))) {
		RTL_W16 (IntrMask, 0);
		goto out;
	}
	/* Acknowledge all of the current interrupt sources ASAP, but
	   an first get an additional status bit from CSCR. */
	if (unlikely(status & RxUnderrun))
		link_changed = RTL_R16 (CSCR) & CSCR_LinkChangeBit;
	ackstat = status & ~ (RxAckBits | TxErr);
	if(ackstat)
		RTL_W16(IntrStatus, ackstat);
	/* Receive packets are processed by poll routine.If not running start it now. */
	if (status & RxAckBits){
		if (napi_schedule_prep(&tp->napi)) {
			RTL_W16_F (IntrMask, rtl8139_norx_intr_mask);
			__napi_schedule(&tp->napi);
		}
	}
	/* Check uncommon events with one test. */
	if (unlikely(status & (PCIErr | PCSTimeout | RxUnderrun | RxErr)))
		rtl8139_weird_interrupt (dev, tp, ioaddr,
					 status, link_changed);
	if (status & (TxOK | TxErr)) {
		rtl8139_tx_interrupt (dev, tp, ioaddr);
		if (status & TxErr)
			RTL_W16 (IntrStatus, TxErr);
	}
out:
	spin_unlock (&tp->lock);
	netdev_dbg(dev, "exiting interrupt, intr_status=%#4.4x\n",
		   RTL_R16(IntrStatus));
	/*The possible return values from an interrupt handler, indicating whether an actual interrupt from the device was present.*/
	return IRQ_RETVAL(handled);    	
}

#ifdef CONFIG_NET_POLL_CONTROLLER
/*
 * Polling receive - used by netconsole and other diagnostic tools
 * to allow network i/o with interrupts disabled.
 */
/* disable_irq — disable an irq and wait for completion : Disable the selected interrupt line. Enables and Disables are nested. This function waits 
   for any pending IRQ handlers for this interrupt to complete before returning. If you use this function while holding a resource the IRQ handler 
   may need you will deadlock. 
*/

/* enable_irq — enable handling of an irq :  Undoes the effect of one call to disable_irq. If this matches the last disable, processing of interrupts on this IRQ line is re-enabled.
  This function may be called from IRQ context only when desc->irq_data.chip->bus_lock and desc->chip->bus_sync_unlock are NULL ! 
*/
static void rtl8139_poll_controller(struct net_device *dev)
{
	struct rtl8139_private *tp = netdev_priv(dev);
	const int irq = tp->pci_dev->irq;

	disable_irq(irq);
	rtl8139_interrupt(irq, dev);
	enable_irq(irq);
}
#endif

/**
 * is_valid_ether_addr - Determine if the given Ethernet address is valid
 * @addr: Pointer to a six-byte array containing the Ethernet address
 *
 * Check that the Ethernet address (MAC) is not 00:00:00:00:00:00, is not
 * a multicast address, and is not FF:FF:FF:FF:FF:FF.
 *
 * Return true if the address is valid.
 */

static int rtl8139_set_mac_address(struct net_device *dev, void *p)
{
	struct rtl8139_private *tp = netdev_priv(dev);
	void __iomem *ioaddr = tp->mmio_addr;
	struct sockaddr *addr = p;
	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;	
	/* Copy ether_addr to dev addr */
	memcpy(dev->dev_addr, addr->sa_data, dev->addr_len);
	
	spin_lock_irq(&tp->lock);

	RTL_W8_F(Cfg9346, Cfg9346_Unlock);
	RTL_W32_F(MAC0 + 0, cpu_to_le32 (*(u32 *) (dev->dev_addr + 0)));
	RTL_W32_F(MAC0 + 4, cpu_to_le32 (*(u32 *) (dev->dev_addr + 4)));
	RTL_W8_F(Cfg9346, Cfg9346_Lock);
	
	spin_unlock_irq(&tp->lock);

	return 0;
}
/**
 *	netif_stop_queue - stop transmitted packets
 *	@dev: network device
 *
 *	Stop upper layers calling the device hard_start_xmit routine.
 *	Used for flow control when transmit resources are unavailable.
 */

/**
 *	napi_disable - prevent NAPI from scheduling
 *	@n: napi context
 *
 * Stop NAPI from being scheduled on this context.
 * Waits till any outstanding processing completes.
 */

static int rtl8139_close (struct net_device *dev)
{
	struct rtl8139_private *tp = netdev_priv(dev);
	void __iomem *ioaddr = tp->mmio_addr;
	unsigned long flags;
	netif_stop_queue(dev);  /*Stop transmitted packets*/
	napi_disable(&tp->napi);
	netif_dbg(tp, ifdown, dev, "Shutting down ethercard, status was 0x%04x\n",
		  RTL_R16(IntrStatus));
	spin_lock_irqsave (&tp->lock, flags);
	
	/* Stop the chip's Tx and Rx DMA processes. */
	RTL_W8 (ChipCmd, 0);
	
	/* Disable interrupts by clearing the interrupt mask. */
	RTL_W16 (IntrMask, 0);
	
	/* Update the error counts. */
	dev->stats.rx_missed_errors += RTL_R32 (RxMissed);
	RTL_W32 (RxMissed, 0);
	
	spin_unlock_irqrestore (&tp->lock, flags);
	
	/*Free IRQ*/
	free_irq(tp->pci_dev->irq, dev);
	
	rtl8139_tx_clear (tp);
	dma_free_coherent(&tp->pci_dev->dev, RX_BUF_TOT_LEN,
			  tp->rx_ring, tp->rx_ring_dma);
	dma_free_coherent(&tp->pci_dev->dev, TX_BUF_TOT_LEN,
			  tp->tx_bufs, tp->tx_bufs_dma);
	tp->rx_ring = NULL;
	tp->tx_bufs = NULL;
	
	/* Green! Put the chip in low-power mode. */
	RTL_W8 (Cfg9346, Cfg9346_Unlock);
	
	if (rtl_chip_info[tp->chipset].flags & HasHltClk)
		RTL_W8 (HltClk, 'H');	/* 'R' would leave the clock running. */

	return 0;
}
/**
 * generic_mii_ioctl - main MII ioctl interface
 * @mii_if: the MII interface
 * @mii_data: MII ioctl data structure
 * @cmd: MII ioctl command
 * @duplex_chg_out: pointer to @duplex_changed status if there was no
 *	ioctl error
 *
 * Returns 0 on success, negative on error.
 */
static int netdev_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct rtl8139_private *tp = netdev_priv(dev);
	int rc;
	if (!netif_running(dev))
		return -EINVAL;
	spin_lock_irq(&tp->lock);
	rc = generic_mii_ioctl(&tp->mii, if_mii(rq), cmd, NULL);
	spin_unlock_irq(&tp->lock);

	return rc;
}

/* Convert net_device_stats to rtnl_link_stats64. rtnl_link_stats64 has
 * all the same fields in the same order as net_device_stats, with only
 * the type differing, but rtnl_link_stats64 may have additional fields
 * at the end for newer counters.
 */

/*
 * In case irq handlers can update u64 counters, readers can use following helpers
 * - SMP 32bit arches use seqcount protection, irq safe.
 * - UP 32bit must disable irqs.
 * - 64bit have no problem atomically reading u64 values, irq safe.
 */
static struct rtnl_link_stats64 *
rtl8139_get_stats64(struct net_device *dev, struct rtnl_link_stats64 *stats)
{
	struct rtl8139_private *tp = netdev_priv(dev);
	void __iomem *ioaddr = tp->mmio_addr;
	unsigned long flags;
	unsigned int start;
	
	if (netif_running(dev)) {
		spin_lock_irqsave (&tp->lock, flags);
		dev->stats.rx_missed_errors += RTL_R32 (RxMissed);
		RTL_W32 (RxMissed, 0);
		spin_unlock_irqrestore (&tp->lock, flags);
	}
	
	netdev_stats_to_stats64(stats, &dev->stats);

	do {
		start = u64_stats_fetch_begin_irq(&tp->rx_stats.syncp);
		stats->rx_packets = tp->rx_stats.packets;
		stats->rx_bytes = tp->rx_stats.bytes;
	} while (u64_stats_fetch_retry_irq(&tp->rx_stats.syncp, start));
	do {
		start = u64_stats_fetch_begin_irq(&tp->tx_stats.syncp);
		stats->tx_packets = tp->tx_stats.packets;
		stats->tx_bytes = tp->tx_stats.bytes;
	} while (u64_stats_fetch_retry_irq(&tp->tx_stats.syncp, start));
	
	return stats;
}
/*
 * Helpers for hash table generation of ethernet nics:
 *
 * Ethernet sends the least significant bit of a byte first, thus crc32_le
 * is used. The output of crc32_le is bit reversed [most significant bit
 * is in bit nr 0], thus it must be reversed before use. Except for
 * nics that bit swap the result internally...
 */

/* Set or clear the multicast filter for this adaptor.
 *    This routine is not state sensitive and need not be SMP locked. */

static void __set_rx_mode (struct net_device *dev)
{
	struct rtl8139_private *tp = netdev_priv(dev);
	void __iomem *ioaddr = tp->mmio_addr;
	u32 mc_filter[2];	/* Multicast hash filter */
	int rx_mode;
	u32 tmp;
	netdev_dbg(dev, "rtl8139_set_rx_mode(%04x) done -- Rx config %08x\n",
		   dev->flags, RTL_R32(RxConfig));
	/* Note: do not reorder, GCC is clever about common statements. */
	/* * @IFF_PROMISC: receive all packets. Can be toggled through sysfs */
 	/* @IFF_ALLMULTI: receive all multicast packets. Can be toggled through sysfs.*/
	/*#define ETH_ALEN	6		Octets in one ethernet addr	  */
	if (dev->flags & IFF_PROMISC) {
		rx_mode =
		    AcceptBroadcast | AcceptMulticast | AcceptMyPhys |
		    AcceptAllPhys;
		mc_filter[1] = mc_filter[0] = 0xffffffff;
	} else if ((netdev_mc_count(dev) > multicast_filter_limit) ||
		   (dev->flags & IFF_ALLMULTI)) {
		/* Too many to filter perfectly -- accept all multicasts. */
		rx_mode = AcceptBroadcast | AcceptMulticast | AcceptMyPhys;
		mc_filter[1] = mc_filter[0] = 0xffffffff;
	} else {
		struct netdev_hw_addr *ha;
		rx_mode = AcceptBroadcast | AcceptMyPhys;
		mc_filter[1] = mc_filter[0] = 0;
		netdev_for_each_mc_addr(ha, dev) {
			int bit_nr = ether_crc(ETH_ALEN, ha->addr) >> 26;  /*ether_crc: # crc32_le (u32 crc(~0) unsigned char const *p,size len);Calculate bitwise little-endian Ethernet AUTODIN II CRC32 */

			mc_filter[bit_nr >> 5] |= 1 << (bit_nr & 31);   /* bit_nr (6)  & (011111) i.e bit_nr 6th will lose*/
			rx_mode |= AcceptMulticast;
		}
	}	
	if (dev->features & NETIF_F_RXALL)                //Receive full frames without stripping the FCS.
		rx_mode |= (AcceptErr | AcceptRunt);
	/* We can safely update without stopping the chip. */
	tmp = rtl8139_rx_config | rx_mode;
	if (tp->rx_config != tmp) {
		RTL_W32_F (RxConfig, tmp);
		tp->rx_config = tmp;
	}
	RTL_W32_F (MAR0 + 0, mc_filter[0]);
	RTL_W32_F (MAR0 + 4, mc_filter[1]);
		
}

static void rtl8139_set_rx_mode (struct net_device *dev)
{
	unsigned long flags;
	struct rtl8139_private *tp = netdev_priv(dev);

	spin_lock_irqsave (&tp->lock, flags);
	__set_rx_mode(dev);
	spin_unlock_irqrestore (&tp->lock, flags);
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


