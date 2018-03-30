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

/**
 *	netif_stop_queue - stop transmitted packets
 *	@dev: network device
 *
 *	Stop upper layers calling the device hard_start_xmit routine.
 *	Used for flow control when transmit resources are unavailable.
 */
static netdev_tx_t rtl8150_start_xmit(struct sk_buff *skb,
					    struct net_device *netdev)
{
	rtl8150_t *dev = netdev_priv(netdev);
	int count, res;

	netif_stop_queue(netdev);
	count = (skb->len < 60) ? 60 : skb->len;
	count = (count & 0x3f) ? count : count + 1; //TODO
	
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
		netif_trans_update(netdev);
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
	short tmpe
	
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
	i, ANLP, 2, &lpa);

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

