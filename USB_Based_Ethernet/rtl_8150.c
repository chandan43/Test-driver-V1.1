#include <linux/signal.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/mii.h>
#include <linux/ethtool.h>
#include <linux/usb.h>
#include <linux/uaccess.h>

/* Version Information */
#define DRIVER_VERSION ".1"
/*page 16 of rtl8150 Dataseet*/
enum REGISTERS {
	IDR	= 0x0120,
	MAR	= 0x0126,
	CR	= 0x012e,
	TCR	= 0x012f,
	RCR	= 0x0130,
	TSR	= 0x0132,
	RSR	= 0x0133,
	CON0	= 0x0135,
	CON1	= 0x0136,
	MSR	= 0x0137,
	PHYADD	= 0x0138,
	PHYDAT	= 0x0139,
	PHYCNT	= 0x013b,
	GPPC	= 0x013d,
	BMCR	= 0x0140,
	BMSR	= 0x0142,
	ANAR	= 0x0144,
	ANLP	= 0x0146,
	AER	= 0x0148,
	CSCR	= 0x014C,
};

#define CSCR_LINK_STATUS 	(1 << 3)
#define	IDR_EEPROM		0x1202

/*page 21 of rtl8150 Dataseet*/
enum PHY_Acess_Control {
	PHY_READ  = 0,
	PHY_WRITE = 0x20,
	PHY_GO	  = 0x40,
};

#define	MII_TIMEOUT		10
#define	INTBUFSIZE		8

/*page 9*/
enum REQ_REGS {
	RTL8150_REQT_READ	= 0xc0,
	RTL8150_REQT_WRITE 	= 0x40,
	RTL8150_REQ_GET_REGS	= 0x05,
	RTL8150_REQ_SET_REGS	= 0x05,
};

/* Transmit status register errors : page 18*/
enum TSR {
	TSR_ECOL		= (1<<5),
	TSR_LCOL		= (1<<4),
	TSR_LOSS_CRS		= (1<<3),
	TSR_JBR			= (1<<2),
	TSR_ERRORS		= (TSR_ECOL | TSR_LCOL | TSR_LOSS_CRS | TSR_JBR),
}; 
/* Receive status register errors : Page 18 - 19 */
enum RSR {
	RSR_CRC			= (1<<2),
	RSR_FAE			= (1<<1),
	RSR_ERRORS		= (RSR_CRC | RSR_FAE),
};
/* Media status register definitions Page 20 */
enum MSR {
	MSR_DUPLEX		= (1<<4),
	MSR_SPEED		= (1<<3),
	MSR_LINK		= (1<<2),
};

/*Interrupt pipe data*/
enum INT {
	INT_TSR			= 0x00,
	INT_RSR			= 0x01,
	INT_MSR			= 0x02,
	INT_WAKSR		= 0x03,
	INT_TXOK_CNT		= 0x04,
	INT_RXLOST_CNT		= 0x05,
	INT_CRERR_CNT		= 0x06,
	INT_COL_CNT		= 0x07,
};

#define	RTL8150_MTU		1540
#define	RTL8150_TX_TIMEOUT	(HZ)
#define	RX_SKB_POOL_SIZE	4

/* rtl8150 flags */
enum RTL_FLAGS {
	RTL8150_HW_CRC		0,
	RX_REG_SET		1,
	RTL8150_UNPLUG		2,
	RX_URB_FAIL		3,
};


/* Define these values to match your device */
enum VENDOR_ID {
	VENDOR_ID_REALTEK	= 0x0bda,
	VENDOR_ID_MELCO		= 0x0411,
	VENDOR_ID_MICRONET	= 0x3980,
	VENDOR_ID_LONGSHINE	= 0x07b8,
	VENDOR_ID_OQO		= 0x1557,
	VENDOR_ID_ZYXEL		= 0x0586,
};

enum PRODUCT_ID {
	PRODUCT_ID_RTL8150	= 0x8150,
	PRODUCT_ID_LUAKTX	= 0x0012,
	PRODUCT_ID_LCS8138TX	= 0x401a,
	PRODUCT_ID_SP128AR	= 0x0003,
	PRODUCT_ID_PRESTIGE 	= 0x401a,
}
static char driver_name [] = "rtl8150";

/* table of devices that work with this driver */
const struct usb_device_id rtl8150_table[] = {
	{USB_DEVICE(VENDOR_ID_REALTEK, PRODUCT_ID_RTL8150)},
	{USB_DEVICE(VENDOR_ID_MELCO, PRODUCT_ID_LUAKTX)},
	{USB_DEVICE(VENDOR_ID_MICRONET, PRODUCT_ID_SP128AR)},
	{USB_DEVICE(VENDOR_ID_LONGSHINE, PRODUCT_ID_LCS8138TX)},
	{USB_DEVICE(VENDOR_ID_OQO, PRODUCT_ID_RTL8150)},
	{USB_DEVICE(VENDOR_ID_ZYXEL, PRODUCT_ID_PRESTIGE)},
	{}
};
MODULE_DEVICE_TABLE(usb, rtl8150_table);

struct rtl8150 {
	unsigned long flags;
	struct usb_device *udev;
	struct tasklet_struct tl;
	struct net_device *netdev;
	struct urb *rx_urb, *tx_urb, *intr_urb;
	struct sk_buff *tx_skb, *rx_skb;
	struct sk_buff *rx_skb_pool[RX_SKB_POOL_SIZE];
	spinlock_t rx_pool_lock;
	struct usb_ctrlrequest dr;
	int intr_interval;
	u8 *intr_buff;
	u8 phy;
};


struct async_req {
	struct usb_ctrlrequest dr;
	u16 rx_creg;
}; 

typedef struct rtl8150  rtl8150_t;
/**
 * usb_control_msg - Builds a control urb, sends it off and waits for completion
 * @dev: pointer to the usb device to send the message to
 * @pipe: endpoint "pipe" to send the message to
 * @request: USB message request value
 * @requesttype: USB message request type value
 * @value: USB message value
 * @index: USB message index value
 * @data: pointer to the data to send
 * @size: length in bytes of the data to send
 * @timeout: time in msecs to wait for the message to complete before timing
 *	out (if 0 the wait is forever)
 *
 * Context: !in_interrupt ()
 *
 * This function sends a simple control message to a specified endpoint and
 * waits for the message to complete, or timeout.
 *
 * Don't use this function from within an interrupt context, like a bottom half
 * handler.  If you need an asynchronous message, or need to send a message
 * from within interrupt context, use usb_submit_urb().
 * If a thread in your driver uses this call, make sure your disconnect()
 * method can wait for it to complete.  Since you don't have a handle on the
 * URB used, you can't cancel the request.
 *
 * Return: If successful, the number of bytes transferred. Otherwise, a negative
 * error number.
 */
/* -----------------------------------------------------------------
 * Read from a register
 * -----------------------------------------------------------------*/
static int get_registers(rtl8150_t *dev, u16 indx, u16 size, void *data)
{
	void *buf;
	int ret;
	
	buf = kmalloc(size, GFP_NOIO);	
	if (!buf)
		return -ENOMEM;
	ret = usb_control_msg(dev->udev,usb_rcvctrlpipe(dev->udev, 0),
			RTL8150_REQ_GET_REGS, RTL8150_REQT_READ,
			indx, 0, buf, size, 500);
	if(ret > 0 && ret <= size)
		memcpy(data, buf, ret);
	
	kfree(buf);
	return ret;
}

/**
 * kmemdup - duplicate region of memory
 *
 * @src: memory region to duplicate
 * @len: memory region length
 * @gfp: GFP mask to use
 */

/* -----------------------------------------------------------------
 * Write to a register
 * -----------------------------------------------------------------*/
static int set_registers(rtl8150_t *dev, u16 indx, u16 size, const void *data)
{
	void *buf;
	int  ret;
	
	buf = kmemdup(data, size, GFP_NOIO);
	if (!buf)
		return -ENOMEM;
	ret = usb_control_msg(dev->udev, usb_sndctrlpipe(dev->udev, 0),
			      RTL8150_REQ_SET_REGS, RTL8150_REQT_WRITE,
			      indx, 0, buf, size, 500);
	kfree(buf);
	return ret;		
} 

static  int rtl8150_probe(struct usb_interface *intf, 
			   const struct usb_device_id *id)
{
	return 0;
}

static void rtl8150_disconnect(struct usb_interface *intf)
{
	
}
static struct usb_driver rtl8150_driver = {
	.name		= driver_name,
	.probe		= rtl8150_probe,
	.disconnect	= rtl8150_disconnect,
	.id_table	= rtl8150_table,
};

MODULE_AUTHOR("Chandan Jha <beingchandanjha@gmail.com>");
MODULE_DESCRIPTION("rtl8150 based usb-ethernet driver");
MODULE_LICENSE("GPL");

