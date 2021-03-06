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
	RTL8150_HW_CRC		= 0,
	RX_REG_SET		= 1,
	RTL8150_UNPLUG		= 2,
	RX_URB_FAIL		= 3,
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
};
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

static void async_set_reg_cb(struct urb *urb)
{
	struct async_req *req = (struct async_req *)urb->context;
	int status = urb->status;

	if (status < 0)
		dev_dbg(&urb->dev->dev, "%s failed with %d", __func__, status);
	kfree(req);
	usb_free_urb(urb);
}

/**
 * usb_alloc_urb - creates a new urb for a USB driver to use
 * @iso_packets: number of iso packets for this urb
 * @mem_flags: the type of memory to allocate, see kmalloc() for a list of
 *	valid options for this.
 *
 * Creates an urb for the USB driver to use, initializes a few internal
 * structures, increments the usage counter, and returns a pointer to it.
 *
 * If the driver want to use this urb for interrupt, control, or bulk
 * endpoints, pass '0' as the number of iso packets.
 *
 * The driver must call usb_free_urb() when it is finished with the urb.
 *
 * Return: A pointer to the new urb, or %NULL if no memory is available.
 */
static int async_set_registers(rtl8150_t *dev, u16 indx, u16 size, u16 reg)
{
	int res = -ENOMEM;
	struct urb *async_urb;
	struct async_req *req;

	req = kmalloc(sizeof(struct async_req), GFP_ATOMIC);
	if (req == NULL)
		return res;
	async_urb = usb_alloc_urb(0, GFP_ATOMIC);
	if (async_urb == NULL) {
		kfree(req);
		return res;
	}
	req->rx_creg = cpu_to_le16(reg);
	req->dr.bRequestType = RTL8150_REQT_WRITE;
	req->dr.bRequest = RTL8150_REQ_SET_REGS;
	req->dr.wIndex = 0;
	req->dr.wValue = cpu_to_le16(indx);
	req->dr.wLength = cpu_to_le16(size);
	usb_fill_control_urb(async_urb, dev->udev,
	                     usb_sndctrlpipe(dev->udev, 0), (void *)&req->dr,
			     &req->rx_creg, size, async_set_reg_cb, req);
	res = usb_submit_urb(async_urb, GFP_ATOMIC);
	if (res) {
		if (res == -ENODEV)
			netif_device_detach(dev->netdev);
		dev_err(&dev->udev->dev, "%s failed with %d\n", __func__, res);
	}
	return res;	
}

static int read_mii_word(rtl8150_t * dev, u8 phy, __u8 indx, u16 * reg)
{
	int i;
	u8 data[3], tmp;
	
	data[0] = phy;
	data[1] = data[2] = 0;
	tmp = indx | PHY_READ | PHY_GO;
	i = 0;
	
	set_registers(dev, PHYADD, sizeof(data), data);
	set_registers(dev, PHYCNT, 1, &tmp);
	do {
		get_registers(dev, PHYCNT, 1, data);
	} while ((data[0] & PHY_GO) && (i++ < MII_TIMEOUT));
	
	if (i <= MII_TIMEOUT) {
		get_registers(dev, PHYDAT, 2, data);
		*reg = data[0] | (data[1] << 8);
		return 0;
	} else
		return 1;	
}

static int write_mii_word(rtl8150_t * dev, u8 phy, __u8 indx, u16 reg)
{
	int i;
	u8 data[3], tmp;

	data[0] = phy;
	data[1] = reg & 0xff; //MSB
	data[2] = (reg >> 8) & 0xff; //LSB
	tmp = indx | PHY_WRITE | PHY_GO;
	i = 0;
	set_registers(dev, PHYADD, sizeof(data), data);
	set_registers(dev, PHYCNT, 1, &tmp);
	
	do {
		get_registers(dev, PHYCNT, 1, data);
	} while ((data[0] & PHY_GO) && (i++ < MII_TIMEOUT));
	if (i <= MII_TIMEOUT)
		return 0;
	else
		return 1;
}
 
static inline void set_ethernet_addr(rtl8150_t *dev)
{
	u8 node_id[6];
	get_registers(dev, IDR, sizeof(node_id), node_id);
	memcpy(dev->netdev->dev_addr, node_id, sizeof(node_id));
}


static int rtl8150_set_mac_address(struct net_device *netdev, void *p)
{
	struct sockaddr *addr = p;
	rtl8150_t *dev = netdev_priv(netdev);

	if (netif_running(netdev))
		return -EBUSY;
	memcpy(netdev->dev_addr, addr->sa_data, netdev->addr_len);
	netdev_dbg(netdev, "Setting MAC address to %pM\n", netdev->dev_addr);
	/* Set the IDR registers. */
	set_registers(dev, IDR, netdev->addr_len, netdev->dev_addr);
#ifdef EEPROM_WRITE
	{
	int i;
	u8 cr;
	/* Get the CR contents. */
	get_registers(dev, CR, 1, &cr);	
	/* Set the WEPROM bit (eeprom write enable). */
	cr |= 0x20;
	set_registers(dev, CR, 1, &cr);
	/* Write the MAC address into eeprom. Eeprom writes must be word-sized,
	   so we need to split them up. */
	for (i = 0; i * 2 < netdev->addr_len; i++) {
		set_registers(dev, IDR_EEPROM + (i * 2), 2,
		netdev->dev_addr + (i * 2));
	}
	/* Clear the WEPROM bit (preventing accidental eeprom writes). */
	cr &= 0xdf;
	set_registers(dev, CR, 1, &cr);
	}
#endif
	return 0;		
}

/* -----------------------------------------------------------------
 * Device Reset function
 * -----------------------------------------------------------------*/
static int rtl8150_reset(rtl8150_t * dev)
{
	u8 data = 0x10;
	int i = HZ;
	set_registers(dev, CR, 1, &data);
	do {
		get_registers(dev, CR, 1, &data);
	} while ((data & 0x10) && --i);
	
	return (i > 0) ? 1 : 0;
}

/* -----------------------------------------------------------------
 * URB : Communication with usb devices  
 * -----------------------------------------------------------------*/

/**
 * usb_alloc_urb - creates a new urb for a USB driver to use
 * @iso_packets: number of iso packets for this urb
 * @mem_flags: the type of memory to allocate, see kmalloc() for a list of
 *	valid options for this.
 *
 * Creates an urb for the USB driver to use, initializes a few internal
 * structures, incrementes the usage counter, and returns a pointer to it.
 *
 * If no memory is available, NULL is returned.
 *
 * If the driver want to use this urb for interrupt, control, or bulk
 * endpoints, pass '0' as the number of iso packets.
 *
 * The driver must call usb_free_urb() when it is finished with the urb.
 */
static int alloc_all_urbs(rtl8150_t * dev)
{
	dev->rx_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!dev->rx_urb)
		return 0;
	dev->tx_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!dev->tx_urb) {
		usb_free_urb(dev->rx_urb);
		return 0;
	}
	dev->intr_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!dev->intr_urb) {
		usb_free_urb(dev->rx_urb);
		usb_free_urb(dev->tx_urb);
		return 0;
	}

	return 1;
}

static void free_all_urbs(rtl8150_t * dev)
{
	usb_free_urb(dev->rx_urb);
	usb_free_urb(dev->tx_urb);
	usb_free_urb(dev->intr_urb);
}

static void unlink_all_urbs(rtl8150_t * dev)
{
	usb_kill_urb(dev->rx_urb);
	usb_kill_urb(dev->tx_urb);
	usb_kill_urb(dev->intr_urb);
}

static inline struct sk_buff *pull_skb(rtl8150_t *dev)
{
	struct sk_buff *skb;
	int i;

	for (i = 0; i < RX_SKB_POOL_SIZE; i++) {
		if (dev->rx_skb_pool[i]) {
			skb = dev->rx_skb_pool[i];
			dev->rx_skb_pool[i] = NULL;
			return skb;
		}
	}
	return NULL;
}

/*
 * To use this rate limit feature of printk  we have to use the function 
 * printk_ratelimit().  The function returns true as long as the limit of 
 * number of messages printed does not excedd the limit set. Once the 
 * limit exceeds the functions starts retuning "0" .
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
 *	netif_rx	-	post buffer to the network code
 *	@skb: buffer to post
 *
 *	This function receives a packet from a device driver and queues it for
 *	the upper (protocol) levels to process.  It always succeeds. The buffer
 *	may be dropped during processing for congestion control or by the
 *	protocol layers.
 *
 *	return values:
 *	NET_RX_SUCCESS	(no congestion)
 *	NET_RX_DROP     (packet was dropped)
 *
 */
static void read_bulk_callback(struct urb *urb)
{
	rtl8150_t *dev;
	unsigned pkt_len, res;
	struct sk_buff *skb;
	struct net_device *netdev;
	u16 rx_stat;
	int status = urb->status;
	int result;
	
	dev = urb->context;
	if (!dev)
		return;
	if (test_bit(RTL8150_UNPLUG, &dev->flags))
		return;
	netdev = dev->netdev;
	if (!netif_device_present(netdev))
		return;
	switch (status) {
		case 0:
			break;
		case -ENOENT:
			return;	/* the urb is in unlink state */
		case -ETIME:
			if (printk_ratelimit())
				dev_warn(&urb->dev->dev, "may be reset is needed?..\n");
			goto goon;
		default:
			if (printk_ratelimit())
				dev_warn(&urb->dev->dev, "Rx status %d\n", status);
			goto goon;
	}
	if (!dev->rx_skb)
		goto resched;
	/* protect against short packets (tell me why we got some?!?) */
	if (urb->actual_length < 4)
		goto goon;
	res = urb->actual_length;
	rx_stat = le16_to_cpu(*(__le16 *)(urb->transfer_buffer + res - 4));
	pkt_len = res - 4;
	
	skb_put(dev->rx_skb, pkt_len);
	dev->rx_skb->protocol = eth_type_trans(dev->rx_skb, netdev);
	netif_rx(dev->rx_skb);

	netdev->stats.rx_packets++;
	netdev->stats.rx_bytes += pkt_len;
	spin_lock(&dev->rx_pool_lock);
	skb = pull_skb(dev); //TODO:
	spin_unlock(&dev->rx_pool_lock);
	if (!skb)
		goto resched;
	
	dev->rx_skb = skb;
goon:
	usb_fill_bulk_urb(dev->rx_urb, dev->udev, usb_rcvbulkpipe(dev->udev, 1),
		      dev->rx_skb->data, RTL8150_MTU, read_bulk_callback, dev);	
	result = usb_submit_urb(dev->rx_urb, GFP_ATOMIC);
	if (result == -ENODEV)
		netif_device_detach(dev->netdev);
	else if (result) {
		set_bit(RX_URB_FAIL, &dev->flags);
		goto resched;
	} else {
		clear_bit(RX_URB_FAIL, &dev->flags);
	}

	return;
resched:
	tasklet_schedule(&dev->tl);	
}
/*
 * It is not allowed to call kfree_skb() or consume_skb() from hardware
 * interrupt context or with hardware interrupts being disabled.
 * (in_irq() || irqs_disabled())
 *
 * We provide four helpers that can be used in following contexts :
 *
 * dev_kfree_skb_irq(skb) when caller drops a packet from irq context,
 *  replacing kfree_skb(skb)
 *
 * dev_consume_skb_irq(skb) when caller consumes a packet from irq context.
 *  Typically used in place of consume_skb(skb) in TX completion path
 *
 * dev_kfree_skb_any(skb) when caller doesn't know its current irq context,
 *  replacing kfree_skb(skb)
 *
 * dev_consume_skb_any(skb) when caller doesn't know its current irq context,
 *  and consumed a packet. Used in place of consume_skb(skb)
 */
/**
 *	netif_wake_queue - restart transmit
 *	@dev: network device
 *
 *	Allow upper layers to call the device hard_start_xmit routine.
 *	Used for flow control when transmit resources are available.
 */
static void write_bulk_callback(struct urb *urb)
{
	rtl8150_t *dev;
	int status = urb->status;

	dev = urb->context;
	if (!dev)
		return;
	dev_kfree_skb_irq(dev->tx_skb); //interupt context
	if (!netif_device_present(dev->netdev))
		return;
	if (status)
		dev_info(&urb->dev->dev, "%s: Tx status %d\n",
				dev->netdev->name, status);
	netif_trans_update(dev->netdev);
	netif_wake_queue(dev->netdev);
}
/**
 *	netif_carrier_ok - test if carrier present
 *	@dev: network device
 *
 * Check if carrier is present on device
 */
/**
 * netif_device_detach - mark device as removed
 * @dev: network device
 *
 * Mark device as removed from system and therefore no longer available.
 */
static void intr_callback(struct urb *urb)
{
	rtl8150_t *dev;
	__u8 *d;
	int status = urb->status;
	int res;

	dev = urb->context;
	
	if (!dev)
		return;
	switch (status) {
	case 0:			/* success */
		break;
	case -ECONNRESET:	/* unlink */
	case -ENOENT:
	case -ESHUTDOWN:
		return;
	/* -EPIPE:  should clear the halt */
	default:
		dev_info(&urb->dev->dev, "%s: intr status %d\n",
			 dev->netdev->name, status);
		goto resubmit;
	}
	d = urb->transfer_buffer;
	if (d[0] & TSR_ERRORS) {
		dev->netdev->stats.tx_errors++;
		if (d[INT_TSR] & (TSR_ECOL | TSR_JBR))
			dev->netdev->stats.tx_aborted_errors++;
		if (d[INT_TSR] & TSR_LCOL)
			dev->netdev->stats.tx_window_errors++;
		if (d[INT_TSR] & TSR_LOSS_CRS)
			dev->netdev->stats.tx_carrier_errors++;	
	}

	/* Report link status changes to the network stack */
	if ((d[INT_MSR] & MSR_LINK) == 0) {
		if (netif_carrier_ok(dev->netdev)) {
			netif_carrier_off(dev->netdev);
			netdev_dbg(dev->netdev, "%s: LINK LOST\n", __func__);
		}
	} else {
		if (!netif_carrier_ok(dev->netdev)) {
			netif_carrier_on(dev->netdev);
			netdev_dbg(dev->netdev, "%s: LINK CAME BACK\n", __func__);
		}
	}
resubmit:
	res = usb_submit_urb (urb, GFP_ATOMIC);
	if (res == -ENODEV)
		netif_device_detach(dev->netdev);
	else if (res)
		dev_err(&dev->udev->dev,
			"can't resubmit intr, %s-%s/input0, status %d\n",
			dev->udev->bus->bus_name, dev->udev->devpath, res);
}

static int rtl8150_suspend(struct usb_interface *intf, pm_message_t message)
{
	rtl8150_t *dev = usb_get_intfdata(intf);
	netif_device_detach(dev->netdev);

	if (netif_running(dev->netdev)) {
		usb_kill_urb(dev->rx_urb);
		usb_kill_urb(dev->intr_urb);
	}
	return 0;
}
/**
 * netif_device_attach - mark device as attached
 * @dev: network device
 *
 * Mark device as attached from system and restart if needed.
 */
static int rtl8150_resume(struct usb_interface *intf)
{
	rtl8150_t *dev = usb_get_intfdata(intf);

	netif_device_attach(dev->netdev);
	if (netif_running(dev->netdev)) {
		dev->rx_urb->status = 0;
		dev->rx_urb->actual_length = 0;
		read_bulk_callback(dev->rx_urb);

		dev->intr_urb->status = 0;
		dev->intr_urb->actual_length = 0;
		intr_callback(dev->intr_urb);
	}
	return 0;
}

/* -----------------------------------------------------------------
*
*	network related part of the code
*
* -----------------------------------------------------------------*/

/**
 *	skb_reserve - adjust headroom
 *	@skb: buffer to alter
 *	@len: bytes to move
 *
 *	Increase the headroom of an empty &sk_buff by reducing the tail
 *	room. This is only allowed for an empty buffer.
 */
static void fill_skb_pool(rtl8150_t *dev)
{
	struct sk_buff *skb;
	int i;
	for (i = 0; i < RX_SKB_POOL_SIZE; i++) {
		if (dev->rx_skb_pool[i])
			continue;
		skb = dev_alloc_skb(RTL8150_MTU + 2);
		if (!skb) {
			return;
		}
		skb_reserve(skb, 2);
		dev->rx_skb_pool[i] = skb;
	}
}
/*
**
**	network related part of the code
**
*/
/*#define dev_kfree_skb(a)	consume_skb(a)*/
/**
 *	consume_skb - free an skbuff
 *	@skb: buffer to free
 *
 *	Drop a ref to the buffer and free it if the usage count has hit zero
 *	Functions identically to kfree_skb, but kfree_skb assumes that the frame
 *	is being dropped after a failure and notes that
 */
static void free_skb_pool(rtl8150_t *dev)
{
	int i;
	for (i = 0; i < RX_SKB_POOL_SIZE; i++)
		if (dev->rx_skb_pool[i])
			dev_kfree_skb(dev->rx_skb_pool[i]);
}
static void rx_fixup(unsigned long data)
{
	struct rtl8150 *dev = (struct rtl8150 *)data;
	struct sk_buff *skb;
	int status;

	spin_lock_irq(&dev->rx_pool_lock);
	fill_skb_pool(dev);
	spin_unlock_irq(&dev->rx_pool_lock);
	
	if (test_bit(RX_URB_FAIL, &dev->flags))
		if (dev->rx_skb)
			goto try_again;
	spin_lock_irq(&dev->rx_pool_lock);
	skb = pull_skb(dev);
	spin_unlock_irq(&dev->rx_pool_lock);
	if (skb == NULL)
		goto tlsched;
	dev->rx_skb = skb;
	usb_fill_bulk_urb(dev->rx_urb, dev->udev, usb_rcvbulkpipe(dev->udev, 1),
		      dev->rx_skb->data, RTL8150_MTU, read_bulk_callback, dev);
	
try_again:
	status = usb_submit_urb(dev->rx_urb, GFP_ATOMIC);
	if (status == -ENODEV) {
		netif_device_detach(dev->netdev);
	} else if (status) {
		set_bit(RX_URB_FAIL, &dev->flags);
		goto tlsched;
	} else {
		clear_bit(RX_URB_FAIL, &dev->flags);
	}

	return;
tlsched:
	tasklet_schedule(&dev->tl);
}

static int enable_net_traffic(rtl8150_t * dev)
{
	u8 cr, tcr, rcr, msr;
	if (!rtl8150_reset(dev)) {
		dev_warn(&dev->udev->dev, "device reset failed\n");
	}
	/* RCR bit7=1 attach Rx info at the end;  =0 HW CRC (which is broken) */
	rcr = 0x9e;
	tcr = 0xd8;
	cr = 0x0c; //Pg 17: CR
	if (!(rcr & 0x80))
		set_bit(RTL8150_HW_CRC, &dev->flags);
	
	set_registers(dev, RCR, 1, &rcr);
	set_registers(dev, TCR, 1, &tcr);
	set_registers(dev, CR, 1, &cr);
	get_registers(dev, MSR, 1, &msr);

	return 0;	
}

static void disable_net_traffic(rtl8150_t * dev)
{
	u8 cr;

	get_registers(dev, CR, 1, &cr);
	cr &= 0xf3; // page 17
	set_registers(dev, CR, 1, &cr);
}


static void rtl8150_tx_timeout(struct net_device *netdev)
{
	rtl8150_t *dev = netdev_priv(netdev);
	dev_warn(&netdev->dev, "Tx timeout.\n");
	usb_unlink_urb(dev->tx_urb);
	netdev->stats.tx_errors++;
}

/**
 *	netif_stop_queue - stop transmitted packets
 *	@dev: network device
 *
 *	Stop upper layers calling the device hard_start_xmit routine.
 *	Used for flow control when transmit resources are unavailable.
 */
/**
 *	netif_wake_queue - restart transmit
 *	@dev: network device
 *
 *	Allow upper layers to call the device hard_start_xmit routine.
 *	Used for flow control when transmit resources are available.
 */
static void rtl8150_set_multicast(struct net_device *netdev)
{
	rtl8150_t *dev = netdev_priv(netdev);
	u16 rx_creg = 0x9e; //page 18 :Receive Configuration Register

	netif_stop_queue(netdev);
	
	if (netdev->flags & IFF_PROMISC) { /* receive all packets */
		rx_creg |= 0x0001;
		dev_info(&netdev->dev, "%s: promiscuous mode\n", netdev->name);
	}else if (!netdev_mc_empty(netdev) ||
		   (netdev->flags & IFF_ALLMULTI)) {  /* receive all multicast packets*/
		rx_creg &= 0xfffe;
		rx_creg |= 0x0002;
		dev_info(&netdev->dev, "%s: allmulti set\n", netdev->name);
	} else {
		/* ~RX_MULTICAST, ~RX_PROMISCUOUS */
		rx_creg &= 0x00fc;
	}
	async_set_registers(dev, RCR, sizeof(rx_creg), rx_creg); //TODO
	netif_wake_queue(netdev);	
}

/**
 *	netif_stop_queue - stop transmitted packets
 *	@dev: network device
 *
 *	Stop upper layers calling the device hard_start_xmit routine.
 *	Used for flow control when transmit resources are unavailable.
 */
/**
 * usb_fill_bulk_urb - macro to help initialize a bulk urb
 * @urb: pointer to the urb to initialize.
 * @dev: pointer to the struct usb_device for this urb.
 * @pipe: the endpoint pipe
 * @transfer_buffer: pointer to the transfer buffer
 * @buffer_length: length of the transfer buffer
 * @complete_fn: pointer to the usb_complete_t function
 * @context: what to set the urb context to.
 *
 * Initializes a bulk urb with the proper information needed to submit it
 * to a device.
 */
static netdev_tx_t rtl8150_start_xmit(struct sk_buff *skb,
					    struct net_device *netdev)
{
	rtl8150_t *dev = netdev_priv(netdev);
	int count, res;

	netif_stop_queue(netdev);
	count = (skb->len < 60) ? 60 : skb->len; 
	/*Max packet size is 64 bytes :  x & 0x3f = 60 and if 64 then count will be 1.*/
	count = (count & 0x3f) ? count : count + 1; 
	
	dev->tx_skb = skb;
	usb_fill_bulk_urb(dev->tx_urb, dev->udev, usb_sndbulkpipe(dev->udev, 2),
		      skb->data, count, write_bulk_callback, dev);
	
	if ((res = usb_submit_urb(dev->tx_urb, GFP_ATOMIC))) {
		/* Can we get/handle EPIPE here? */
		if (res == -ENODEV)
			netif_device_detach(dev->netdev);
		else {
			dev_warn(&netdev->dev, "failed tx_urb %d\n", res);
			netdev->stats.tx_errors++;
			netif_start_queue(netdev); 
		}
	} else {
		netdev->stats.tx_packets++;
		netdev->stats.tx_bytes += skb->len;
		netif_trans_update(netdev); //Updating 
	}

	return NETDEV_TX_OK;
}
/**
 *	netif_carrier_on - set carrier
 *	@dev: network device
 *
 * Device has detected that carrier.
 */
static void set_carrier(struct net_device *netdev)
{
	rtl8150_t *dev = netdev_priv(netdev);
	short tmp;
	
	get_registers(dev, CSCR, 2, &tmp);
	if (tmp & CSCR_LINK_STATUS)
		netif_carrier_on(netdev);
	else
		netif_carrier_off(netdev);	
}
/**
 * usb_fill_bulk_urb - macro to help initialize a bulk urb
 * @urb: pointer to the urb to initialize.
 * @dev: pointer to the struct usb_device for this urb.
 * @pipe: the endpoint pipe
 * @transfer_buffer: pointer to the transfer buffer
 * @buffer_length: length of the transfer buffer
 * @complete_fn: pointer to the usb_complete_t function
 * @context: what to set the urb context to.
 *
 * Initializes a bulk urb with the proper information needed to submit it
 * to a device.
 */
/*-------------------------------------------------------------------*/

/**
 * usb_submit_urb - issue an asynchronous transfer request for an endpoint
 * @urb: pointer to the urb describing the request
 * @mem_flags: the type of memory to allocate, see kmalloc() for a list
 *	of valid options for this.
 *
 * This submits a transfer request, and transfers control of the URB
 * describing that request to the USB subsystem.  Request completion will
 * be indicated later, asynchronously, by calling the completion handler.
 * The three types of completion are success, error, and unlink
 * (a software-induced fault, also called "request cancellation").
 *
 * URBs may be submitted in interrupt context.
 */
/**
 * netif_device_detach - mark device as removed
 * @dev: network device
 *
 * Mark device as removed from system and therefore no longer available.
 */
/**
 *	netif_start_queue - allow transmit
 *	@dev: network device
 *
 *	Allow upper layers to call the device hard_start_xmit routine.
 */

static int rtl8150_open(struct net_device *netdev)
{
	rtl8150_t *dev = netdev_priv(netdev);
	int res;
	
	if (dev->rx_skb == NULL)
		dev->rx_skb = pull_skb(dev);
	if (!dev->rx_skb)
		return -ENOMEM;
	set_registers(dev, IDR, 6, netdev->dev_addr);
	usb_fill_bulk_urb(dev->rx_urb, dev->udev, usb_rcvbulkpipe(dev->udev, 1),
		      dev->rx_skb->data, RTL8150_MTU, read_bulk_callback, dev);
	if ((res = usb_submit_urb(dev->rx_urb, GFP_KERNEL))) {
		if (res == -ENODEV)
			netif_device_detach(dev->netdev);
		dev_warn(&netdev->dev, "rx_urb submit failed: %d\n", res);
		return res;
	}
	usb_fill_int_urb(dev->intr_urb, dev->udev, usb_rcvintpipe(dev->udev, 3),
		     dev->intr_buff, INTBUFSIZE, intr_callback,
		     dev, dev->intr_interval);
	if ((res = usb_submit_urb(dev->intr_urb, GFP_KERNEL))) {
		if (res == -ENODEV)
			netif_device_detach(dev->netdev);
		dev_warn(&netdev->dev, "intr_urb submit failed: %d\n", res);
		usb_kill_urb(dev->rx_urb);
		return res;
	}
	enable_net_traffic(dev); //TODO:
	set_carrier(netdev); //TODO
	netif_start_queue(netdev);

	return res;
}

#if 0 /* Fool kernel-doc since it doesn't do macros yet */
/**
 * test_bit - Determine whether a bit is set
 * @nr: bit number to test
 * @addr: Address to start counting from
 */
static int test_bit(int nr, const volatile unsigned long *addr);
#endif

static int rtl8150_close(struct net_device *netdev)
{
	rtl8150_t *dev = netdev_priv(netdev);

	netif_stop_queue(netdev);
	if (!test_bit(RTL8150_UNPLUG, &dev->flags)) 
		disable_net_traffic(dev);
	unlink_all_urbs(dev);

	return 0;	

}

/**
 * usb_make_path - returns stable device path in the usb tree
 * @dev: the device whose path is being constructed
 * @buf: where to put the string
 * @size: how big is "buf"?
 *
 * Return: Length of the string (> 0) or negative if size was too small.
 *
 * Note:
 * This identifier is intended to be "stable", reflecting physical paths in
 * hardware such as physical bus addresses for host controllers or ports on
 * USB hubs.  That makes it stay the same until systems are physically
 * reconfigured, by re-cabling a tree of USB devices or by moving USB host
 * controllers.  Adding and removing devices, including virtual root hubs
 * in host controller driver modules, does not change these path identifiers;
 * neither does rebooting or re-enumerating.  These are more useful identifiers
 * than changeable ("unstable") ones like bus numbers or device addresses.
 *
 * With a partial exception for devices connected to USB 2.0 root hubs, these
 * identifiers are also predictable.  So long as the device tree isn't changed,
 * plugging any USB device into a given hub port always gives it the same path.
 * Because of the use of "companion" controllers, devices connected to ports on
 * USB 2.0 root hubs (EHCI host controllers) will get one path ID if they are
 * high speed, and a different one if they are full or low speed.
 */

static void rtl8150_get_drvinfo(struct net_device *netdev, struct ethtool_drvinfo *info)
{
	rtl8150_t *dev = netdev_priv(netdev);
	strlcpy(info->driver, driver_name, sizeof(info->driver));
	strlcpy(info->version, DRIVER_VERSION, sizeof(info->version));
	usb_make_path(dev->udev, info->bus_info, sizeof(info->bus_info));
}

static int rtl8150_get_link_ksettings(struct net_device *netdev,
				      struct ethtool_link_ksettings *ecmd)
{
	rtl8150_t *dev = netdev_priv(netdev);
	/* Link partner ability register. */
	short lpa, bmcr;
	u32 supported;


	/**/	
	supported = (SUPPORTED_10baseT_Half |
			  SUPPORTED_10baseT_Full |
			  SUPPORTED_100baseT_Half |
			  SUPPORTED_100baseT_Full |
			  SUPPORTED_Autoneg |
			  SUPPORTED_TP | SUPPORTED_MII);
	ecmd->base.port = PORT_TP;
	ecmd->base.phy_address = dev->phy;

	get_registers(dev, BMCR, 2, &bmcr);
	get_registers(dev, ANLP, 2, &lpa);

	if (bmcr & BMCR_ANENABLE) {
		u32 speed = ((lpa & (LPA_100HALF | LPA_100FULL)) ?
			     SPEED_100 : SPEED_10);
		ecmd->base.speed = speed;
		ecmd->base.autoneg = AUTONEG_ENABLE;
		if (speed == SPEED_100)
			ecmd->base.duplex = (lpa & LPA_100FULL) ?
			    DUPLEX_FULL : DUPLEX_HALF;
		else
			ecmd->base.duplex = (lpa & LPA_10FULL) ?
			    DUPLEX_FULL : DUPLEX_HALF;
	} else {
		ecmd->base.autoneg = AUTONEG_DISABLE;
		ecmd->base.speed = ((bmcr & BMCR_SPEED100) ?
					     SPEED_100 : SPEED_10);
		ecmd->base.duplex = (bmcr & BMCR_FULLDPLX) ?
		    DUPLEX_FULL : DUPLEX_HALF;
	}
	ethtool_convert_legacy_u32_to_link_mode(ecmd->link_modes.supported,
						supported);

	return 0;	
}


static const struct ethtool_ops ops = {
	.get_drvinfo = rtl8150_get_drvinfo,
	.get_link = ethtool_op_get_link,
	.get_link_ksettings = rtl8150_get_link_ksettings,
};

static int rtl8150_ioctl(struct net_device *netdev, 
				struct ifreq *rq, int cmd)
{
	rtl8150_t *dev = netdev_priv(netdev);
	u16 *data = (u16 *) & rq->ifr_ifru;
	int res = 0;

	switch (cmd) {
		case SIOCDEVPRIVATE:
			data[0] = dev->phy;
		case SIOCDEVPRIVATE + 1:
			read_mii_word(dev, dev->phy, (data[1] & 0x1f), &data[3]);
			break;
		case SIOCDEVPRIVATE + 2:
			if (!capable(CAP_NET_ADMIN))
				return -EPERM;
			write_mii_word(dev, dev->phy, (data[1] & 0x1f), data[2]);
			break;
		default: 
			res = -EOPNOTSUPP;
	}
	return res;
}

static const struct net_device_ops rtl8150_netdev_ops = {
	.ndo_open		= rtl8150_open,
	.ndo_stop		= rtl8150_close,
	.ndo_do_ioctl		= rtl8150_ioctl,
	.ndo_start_xmit		= rtl8150_start_xmit,
	.ndo_tx_timeout		= rtl8150_tx_timeout,
	.ndo_set_rx_mode	= rtl8150_set_multicast,
	.ndo_set_mac_address	= rtl8150_set_mac_address,

	.ndo_validate_addr	= eth_validate_addr,
};

/**
 *	netdev_priv - access network device private data
 *	@dev: network device
 *
 * Get network device private data
 */
static  int rtl8150_probe(struct usb_interface *intf, 
			   const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	rtl8150_t *dev;
	struct net_device *netdev;
		
	netdev = alloc_etherdev(sizeof(rtl8150_t));

	if (!netdev)
		return -ENOMEM;

	dev = netdev_priv(netdev);
	
	dev->intr_buff = kmalloc(INTBUFSIZE, GFP_KERNEL);
	if (!dev->intr_buff) {
		free_netdev(netdev);
		return -ENOMEM;
	}
	
	tasklet_init(&dev->tl, rx_fixup, (unsigned long)dev);
	spin_lock_init(&dev->rx_pool_lock);

	dev->udev = udev;
	dev->netdev = netdev;
	netdev->netdev_ops = &rtl8150_netdev_ops;
	netdev->watchdog_timeo = RTL8150_TX_TIMEOUT;
	netdev->ethtool_ops = &ops;
	dev->intr_interval = 100;	/* 100ms */

	/*TODO:*/
	if (!alloc_all_urbs(dev)) {
		dev_err(&intf->dev, "out of memory\n");
		goto out;
	}
	if (!rtl8150_reset(dev)) {
		dev_err(&intf->dev, "couldn't reset the device\n");
		goto out1;
	}
	fill_skb_pool(dev);	//TODO

	
	usb_set_intfdata(intf, dev);
	
	/* Set the sysfs physical device reference for the network logical device
 	* if set prior to registration will cause a symlink during initialization.
 	*/
	SET_NETDEV_DEV(netdev, &intf->dev);
	if (register_netdev(netdev) != 0) {
		dev_err(&intf->dev, "couldn't register the device\n");
		goto out2;
	}
	dev_info(&intf->dev, "%s: rtl8150 is detected\n", netdev->name);
	return 0;
out2:
	usb_set_intfdata(intf, NULL);
	free_skb_pool(dev);
out1:
	free_all_urbs(dev);
out:
	kfree(dev->intr_buff);
	free_netdev(netdev);
	return -EIO;
}

static void rtl8150_disconnect(struct usb_interface *intf)
{
	rtl8150_t *dev = usb_get_intfdata(intf);

	usb_set_intfdata(intf, NULL);
	if (dev) {
		set_bit(RTL8150_UNPLUG, &dev->flags);
		tasklet_kill(&dev->tl);
		unregister_netdev(dev->netdev);
		unlink_all_urbs(dev);
		free_all_urbs(dev); //TODO
		free_skb_pool(dev); //TODO
		if (dev->rx_skb)
			dev_kfree_skb(dev->rx_skb);
		kfree(dev->intr_buff);
		free_netdev(dev->netdev);
	}
			
}
static struct usb_driver rtl8150_driver = {
	.name		= driver_name,
	.probe		= rtl8150_probe,
	.disconnect	= rtl8150_disconnect,
	.id_table	= rtl8150_table,
	.suspend	= rtl8150_suspend,
	.resume		= rtl8150_resume,
	.disable_hub_initiated_lpm = 1,
};

module_usb_driver(rtl8150_driver);

MODULE_AUTHOR("Chandan Jha <beingchandanjha@gmail.com>");
MODULE_DESCRIPTION("rtl8150 based usb-ethernet driver");
MODULE_LICENSE("GPL");

