/* This driver is based on the 2.6.3 version of drivers drivers/bluetooth/btusb.c
 * but has been rewritten to be easier to read and use
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/snvb.h>
#include <linux/spinlock.h>
#include <linux/usb.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>


#if 0
#References : 
    1.  http://www.usb.org/developers/defined_class
    2.  https://elixir.free-electrons.com/linux/v3.4/source/drivers/bluetooth/btusb.c
    3.  https://www.kernel.org/doc/Documentation/usb/anchors.txt
#endif

static bool ignore_dga;
static bool ignore_csr;
static bool ignore_sniffer;
static bool disable_scofix;
static bool force_scofix;

static bool reset = 1;

static struct usb_driver btusb_driver;

#define BTUSB_IGNORE		0x01
#define BTUSB_DIGIANSWER	0x02
#define BTUSB_CSR		0x04
#define BTUSB_SNIFFER		0x08
#define BTUSB_BCM92035		0x10
#define BTUSB_BROKEN_ISOC	0x20
#define BTUSB_WRONG_SCO_MTU	0x40
#define BTUSB_ATH3012		0x80

/**
 * USB_DEVICE_INFO - macro used to describe a class of usb devices
 * @cl: bDeviceClass value
 * @sc: bDeviceSubClass value
 * @pr: bDeviceProtocol value
 *
 * This macro is used to create a struct usb_device_id that matches a
 * specific class of devices.
 */

/**
 * USB_DEVICE - macro used to describe a specific usb device
 * @vend: the 16 bit USB Vendor ID
 * @prod: the 16 bit USB Product ID
 *
 * This macro is used to create a struct usb_device_id that matches a
 * specific device.
 */
/*The Bluetooth host and Bluetooth controller communicate with the help of the HCI. 
  It contains drivers that  abstract and transfer data between the Bluetooth host and 
  the Bluetooth controller. These drivers implement communication between the Bluetooth 
  host and the Bluetooth controller with a small set of functions that send and receive 
  commands, data packets and events.Communication between the host and the controller 
  is done through HCI packets, of which there are four types.
 */

static const struct usb_device_id btusb_table[] = {
	/* Generic Bluetooth USB device*/
	{ USB_DEVICE_INFO(0xe0, 0x01, 0x01) },
	{ } /* Terminating entry */
};

MODULE_DEVICE_TABLE(usb, btusb_table);

static struct usb_device_id blacklist_table[] = {
	/* CSR BlueCore devices */
	{ USB_DEVICE(0x0a12, 0x0001), .driver_info = BTUSB_CSR },

	/* Broadcom BCM2033 without firmware */
	{ USB_DEVICE(0x0a5c, 0x2033), .driver_info = BTUSB_IGNORE },

	/* Atheros 3011 with sflash firmware */
	{ USB_DEVICE(0x0cf3, 0x3002), .driver_info = BTUSB_IGNORE },
	{ USB_DEVICE(0x13d3, 0x3304), .driver_info = BTUSB_IGNORE },
	{ USB_DEVICE(0x0930, 0x0215), .driver_info = BTUSB_IGNORE },
	{ USB_DEVICE(0x0489, 0xe03d), .driver_info = BTUSB_IGNORE },

	/* Atheros AR9285 Malbec with sflash firmware */
	{ USB_DEVICE(0x03f0, 0x311d), .driver_info = BTUSB_IGNORE },

	/* Atheros 3012 with sflash firmware */
	{ USB_DEVICE(0x0cf3, 0x3004), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x0cf3, 0x311d), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x13d3, 0x3375), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x04ca, 0x3005), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x13d3, 0x3362), .driver_info = BTUSB_ATH3012 },
	{ USB_DEVICE(0x0cf3, 0xe004), .driver_info = BTUSB_ATH3012 },

	/* Atheros AR5BBU12 with sflash firmware */
	{ USB_DEVICE(0x0489, 0xe02c), .driver_info = BTUSB_IGNORE },

	/* Broadcom BCM2035 */
	{ USB_DEVICE(0x0a5c, 0x2035), .driver_info = BTUSB_WRONG_SCO_MTU },
	{ USB_DEVICE(0x0a5c, 0x200a), .driver_info = BTUSB_WRONG_SCO_MTU },
	{ USB_DEVICE(0x0a5c, 0x2009), .driver_info = BTUSB_BCM92035 },

	/* Broadcom BCM2045 */
	{ USB_DEVICE(0x0a5c, 0x2039), .driver_info = BTUSB_WRONG_SCO_MTU },
	{ USB_DEVICE(0x0a5c, 0x2101), .driver_info = BTUSB_WRONG_SCO_MTU },

	/* IBM/Lenovo ThinkPad with Broadcom chip */
	{ USB_DEVICE(0x0a5c, 0x201e), .driver_info = BTUSB_WRONG_SCO_MTU },
	{ USB_DEVICE(0x0a5c, 0x2110), .driver_info = BTUSB_WRONG_SCO_MTU },

	/* HP laptop with Broadcom chip */
	{ USB_DEVICE(0x03f0, 0x171d), .driver_info = BTUSB_WRONG_SCO_MTU },

	/* Dell laptop with Broadcom chip */
	{ USB_DEVICE(0x413c, 0x8126), .driver_info = BTUSB_WRONG_SCO_MTU },

	/* Dell Wireless 370 and 410 devices */
	{ USB_DEVICE(0x413c, 0x8152), .driver_info = BTUSB_WRONG_SCO_MTU },
	{ USB_DEVICE(0x413c, 0x8156), .driver_info = BTUSB_WRONG_SCO_MTU },

	/* Belkin F8T012 and F8T013 devices */
	{ USB_DEVICE(0x050d, 0x0012), .driver_info = BTUSB_WRONG_SCO_MTU },
	{ USB_DEVICE(0x050d, 0x0013), .driver_info = BTUSB_WRONG_SCO_MTU },

	/* Asus WL-BTD202 device */
	{ USB_DEVICE(0x0b05, 0x1715), .driver_info = BTUSB_WRONG_SCO_MTU },

	/* Kensington Bluetooth USB adapter */
	{ USB_DEVICE(0x047d, 0x105e), .driver_info = BTUSB_WRONG_SCO_MTU },

	/* RTX Telecom based adapters with buggy SCO support */
	{ USB_DEVICE(0x0400, 0x0807), .driver_info = BTUSB_BROKEN_ISOC },
	{ USB_DEVICE(0x0400, 0x080a), .driver_info = BTUSB_BROKEN_ISOC },

	/* CONWISE Technology based adapters with buggy SCO support */
	{ USB_DEVICE(0x0e5e, 0x6622), .driver_info = BTUSB_BROKEN_ISOC },

	/* Digianswer devices */
	{ USB_DEVICE(0x08fd, 0x0001), .driver_info = BTUSB_DIGIANSWER },
	{ USB_DEVICE(0x08fd, 0x0002), .driver_info = BTUSB_IGNORE },

	/* CSR BlueCore Bluetooth Sniffer */
	{ USB_DEVICE(0x0a12, 0x0002), .driver_info = BTUSB_SNIFFER },

	/* Frontline ComProbe Bluetooth Sniffer */
	{ USB_DEVICE(0x16d3, 0x0002), .driver_info = BTUSB_SNIFFER },

	{ }	/* Terminating entry */
};

#define BTUSB_MAX_ISOC_FRAMES	10              // TODO : 

#define BTUSB_INTR_RUNNING	0
#define BTUSB_BULK_RUNNING	1
#define BTUSB_ISOC_RUNNING	2
#define BTUSB_SUSPENDING	3
#define BTUSB_DID_ISO_RESUME	4

/*The anchor is a data structure takes care of
 * keeping track of URBs and provides methods to deal with
 * multiple URBs.*/

struct btusb_data {
	struct hci_dev       *hdev;
	struct usb_device    *udev;
	struct usb_interface *intf;
	struct usb_interface *isoc;

	spinlock_t lock;

	unsigned long flags;

	struct work_struct work;
	struct work_struct waker;

	struct usb_anchor tx_anchor;
	struct usb_anchor intr_anchor;
	struct usb_anchor bulk_anchor;
	struct usb_anchor isoc_anchor;
	struct usb_anchor deferred;
	int tx_in_flight;
	spinlock_t txlock;

	struct usb_endpoint_descriptor *intr_ep;
	struct usb_endpoint_descriptor *bulk_tx_ep;
	struct usb_endpoint_descriptor *bulk_rx_ep;
	struct usb_endpoint_descriptor *isoc_tx_ep;
	struct usb_endpoint_descriptor *isoc_rx_ep;

	__u8 cmdreq_type;

	unsigned int sco_num;
	int isoc_altsetting;
	int suspend_count;
};

/* spin_lock_irqsave is basically used to save the interrupt state 
 * before taking the spin lock, this is because spin lock disables 
 * the interrupt, when the lock is taken in interrupt context, and 
 * re-enables it when while unlocking. The interrupt state is saved 
 * so that it should reinstate the interrupts again. 
 */
static int inc_tx(struct btusb_data *data)
{
	unsigned long flags;
	int rv;
	spin_lock_irqsave(&data->txlock, flags);

	rv = test_bit(BTUSB_SUSPENDING, &data->flags);
	if(!rv)
		data->tx_in_flight++;
	spin_unlock_irqrestore(&data->txlock, flags);
	return rv;
}
/**
 * struct usb_ctrlrequest - SETUP data for a USB device control request
 * @bRequestType: matches the USB bmRequestType field
 * @bRequest: matches the USB bRequest field
 * @wValue: matches the USB wValue field (le16 byte order)
 * @wIndex: matches the USB wIndex field (le16 byte order)
 * @wLength: matches the USB wLength field (le16 byte order)
 *
 * This structure is used to send control requests to a USB device.  It matches
 * the different fields of the USB 2.0 Spec section 9.3, table 9-2.  See the
 * USB spec for a fuller description of the different fields, and what they are
 * used for.
 *
 * Note that the driver for any interface can issue control requests.
 * For most devices, interfaces don't coordinate with each other, so
 * such requests may be made at any time.
struct usb_ctrlrequest {
	__u8 bRequestType;
	__u8 bRequest;
	__le16 wValue;
	__le16 wIndex;
	__le16 wLength;
} __attribute__ ((packed));
 * usb_fill_control_urb - initializes a control urb
 * @urb: pointer to the urb to initialize.
 * @dev: pointer to the struct usb_device for this urb.
 * @pipe: the endpoint pipe
 * @setup_packet: pointer to the setup_packet buffer
 * @transfer_buffer: pointer to the transfer buffer
 * @buffer_length: length of the transfer buffer
 * @complete_fn: pointer to the usb_complete_t function
 * @context: what to set the urb context to.
 *
 * Initializes a control urb with the proper information needed to submit
 * it to a device.
 */
/**
 * usb_anchor_urb - anchors an URB while it is processed
 * @urb: pointer to the urb to anchor
 * @anchor: pointer to the anchor
 *
 * This can be called to have access to URBs which are to be executed
 * without bothering to track them
 */
/*
	A USB driver needs to support some callbacks requiring
	a driver to cease all IO to an interface. To do so, a
	driver has to keep track of the URBs it has submitted
	to know they've all completed or to call usb_kill_urb
	for them. The anchor is a data structure takes care of
	keeping track of URBs and provides methods to deal with
	multiple URBs.
	
	An association of URBs to an anchor is made by an explicit
	call to usb_anchor_urb(). The association is maintained until
	an URB is finished by (successful) completion.
*/
 /* This submits a transfer request, and transfers control of the URB
  * describing that request to the USB subsystem.  Request completion will
  * be indicated later, asynchronously, by calling the completion handler.
  * The three types of completion are success, error, and unlink
  * (a software-induced fault, also called "request cancellation").
  */
 /*The driver marks the device busy as it receives data and then processes 
   the received data. This way, autosuspend is attempted only if no input 
   or output was performed for the duration of the configurable delay.
 
   Waking up a device has some cost in time and power; it takes about 40ms 
   to wake up the device. Therefore staying in the suspended mode for less 
   than a few seconds is not sensible. As already mentioned, there's a 
   configurable delay between the time the counters reach zero and autosuspend 
   is attempted. When using remote wakeup, however, the counters remain at zero 
   all the time unless they are incremented due to output. Yet a delay after the 
   last time a device is busy, that is, does I/O, and the next attempt to autosuspend 
   the device is highly desirable.
*/
int btusb_send_frame(struct sk_buff *skb)
{
	struct hci_dev *hdev = (struct hci_dev *) skb->dev;
	struct btusb_data *data = hci_get_drvdata(hdev);
	struct usb_ctrlrequest *dr; /*SETUP data for a USB device control request */
	struct urb *urb; /* The basic idea of the new driver is message passing,  */
	unsigned int pipe;
	int err;

	BT_DBG("%s", hdev->name);

	if(!test_bit(HCI_RUNNING, &hdev->flags ))  //Means HCI is not running .!
		return -EBUSY;
	/* Common control buffer , free to use any layer
	 * Bluetooth proto is also use 
	 * bt_cb(skb) (struct bt_skb_cb *)((skb)->cb))*/
	switch(bt_cb(skb)->pkt_type){
		/*Command send to host to bluetooth */
		case HCI_COMMAND_PKT:
			urb = usb_alloc_urb(0, GFP_ATOMIC); /* URBs are allocated : Param2: The number of isochronous transfer frames you want to schedule.*/
			if (!urb)
				return -ENOMEM;
			dr=kmalloc(sizeof(*dr),GFP_ATOMIC);
			if(!dr){
				usb_free_urb(urb);
				return -ENOMEM;
			}
			dr->bRequestType = data->cmdreq_type;
			dr->bRequest     = 0;
			dr->wIndex       = 0;
			dr->wValue       = 0;
			dr->wLength      = __cpu_to_le16(skb->len);
			/* Create pipes... */
			pipe = usb_sndctrlpipe(data->udev, 0x00);
			/* initializes a control urb */
			usb_fill_control_urb(urb, data->udev, pipe, (void *) dr,
				skb->data, skb->len, btusb_tx_complete, skb);
			hdev->stat.cmd_tx++;
			break;
		/*Asyncronous data which is  sent to device */
		case HCI_ACLDATA_PKT:
			if(!data->bulk_tx_ep)
				return -ENODEV;
			urb = usb_alloc_urb(0, GFP_ATOMIC); /* URBs are allocated : Param2: The number of isochronous transfer frames you want to schedule.*/
			if (!urb)
				return -ENOMEM;
			pipe = usb_sndbulkpipe(data->udev,
					data->bulk_tx_ep->bEndpointAddress);
			usb_fill_bulk_urb(urb, data->udev, pipe,
				skb->data, skb->len, btusb_tx_complete, skb);

			hdev->stat.acl_tx++;
			break;	
		/* syncronous data which is  sent or receive to device*/
		case HCI_SCODATA_PKT:
			if (!data->isoc_tx_ep || hdev->conn_hash.sco_num < 1)
				return -ENODEV;

			urb = usb_alloc_urb(BTUSB_MAX_ISOC_FRAMES, GFP_ATOMIC);
			if (!urb)
				return -ENOMEM
			usb_fill_int_urb(urb, data->udev, pipe,
				skb->data, skb->len, btusb_isoc_tx_complete,
				skb, data->isoc_tx_ep->bInterval);

			urb->transfer_flags  = URB_ISO_ASAP;
			__fill_isoc_descriptor(urb, skb->len,                                //TODO
					le16_to_cpu(data->isoc_tx_ep->wMaxPacketSize));
			hdev->stat.sco_tx++;
			goto skip_waking;
		default:
			return -EILSEQ;
		}
	err = inc_tx(data);
	if (err) {
		usb_anchor_urb(urb, &data->deferred);
		schedule_work(&data->waker);
		err = 0;
		goto done;
	}

skip_waking:
	usb_anchor_urb(urb, &data->tx_anchor);
	/*An association of URBs to an anchor is made by an explicit
          call to usb_anchor_urb(). The association is maintained until
	  an URB is finished by (successful) completion.*/
	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err < 0) {
		if (err != -EPERM && err != -ENODEV)
			BT_ERR("%s urb %p submission failed (%d)",
						hdev->name, urb, -err);
		kfree(urb->setup_packet);
		usb_unanchor_urb(urb);
	} else {
		usb_mark_last_busy(data->udev);
	}
done:
	usb_free_urb(urb);
	return err;
}	


/**
 * schedule_work - put work task in global workqueue
 * @work: job to be done
 *
 * Returns zero if @work was already on the kernel-global workqueue and
 * non-zero otherwise.
 *
 * This puts a job in the kernel-global workqueue if it was not already
 * queued and leaves it in the same position on the kernel-global
 * workqueue otherwise.
 */
		
struct void btusb_notify(struct hci_dev *hdev, unsigned int evt)
{
	struct btusb_data *data = hci_get_drvdata(hdev);

	BT_DBG("%s evt %d", hdev->name, evt);
	
	if(hdev->conn_hash.sco_num != data->sco_num){
		data->sco_num = hdev->conn_hash.sco_num;
		schedule_work(&data->work);
	}
}
		
/**
 * usb_set_interface - Makes a particular alternate setting be current
 * @dev: the device whose interface is being updated
 * @interface: the interface being updated
 * @alternate: the setting being chosen.
 * Context: !in_interrupt ()
 */
static inline int __set_isoc_interface(struct hci_dev *hdev, int altsetting)
{
	struct btusb_data *data = hci_get_drvdata(hdev);
	struct usb_interface *intf = data->isoc;
	struct usb_endpoint_descriptor *ep_desc;
	int i, err;
	if (!data->isoc)
		return -ENODEV;
	err = usb_set_interface(data->udev, 1, altsetting);
	if(err < 0){
		BT_ERR("%s setting interface failed (%d)", hdev->name, -err);
		return err;
	}
	data->isoc_altsetting = altsetting;

	data->isoc_tx_ep = NULL;
	data->isoc_rx_ep = NULL;
	for (i = 0; i < intf->cur_altsetting->desc.bNumEndpoints; i++) {
		ep_desc = &intf->cur_altsetting->endpoint[i].desc;
		
		if(!data->isoc_tx_ep &&  usb_endpoint_is_isoc_out(ep_desc)){
			data->isoc_tx_ep = ep_desc;
			continue;
		}
		if(!data->isoc_rx_ep && usb_endpoint_is_isoc_in(ep_desc)){
			data->isoc_rx_ep = ep_desc;
			continue;
		}
	}
	
	if(!data->isoc_tx_ep ||  !data->isoc_rx_ep ){
		BT_ERR("%s invalid SCO descriptors", hdev->name);
		return -ENODEV;
	}

	return 0;
}
/* ACL= Asynchronous Connection-Less. SCO = Synchronous Connection Oriented.
   SCO is Point to Point Connection between only one master and only one slave.
   ACL is multipoint connection between one master and many slaves.
*/
/*test_bit() will return 1 or 0 to denotewhether a bit is set or not in a bitmap.*/
static void btusb_work(struct work_struct *work)
{
	struct btusb_data *data = container_of(work, struct btusb_data, work);
	struct hci_dev *hdev = data->hdev;
	int err;
		
	if (hdev->conn_hash.sco_num > 0) {
		if(!test_bit(BTUSB_DID_ISO_RESUME, &data->flags)){
			err = usb_autopm_get_interface(data->isoc ? data->isoc : data->intf);
			if(err < 0){
				clear_bit(BTUSB_ISOC_RUNNING, &data->flags); 
				usb_kill_anchored_urbs(&data->isoc_anchor);
				return;
			}
			set_bit(BTUSB_DID_ISO_RESUME, &data->flags);
		}
		if(data->isoc_altsetting != 2) {
			clear_bit(BTUSB_ISOC_RUNNING, &data->flags);
			usb_kill_anchored_urbs(&data->isoc_anchor);
			
			if (__set_isoc_interface(hdev, 2) < 0)     //TODO
				return;
		}
		if (!test_and_set_bit(BTUSB_ISOC_RUNNING, &data->flags)) {  // Set a bit and return its old value : if unset 
			if (btusb_submit_isoc_urb(hdev, GFP_KERNEL) < 0)    //TODO
				clear_bit(BTUSB_ISOC_RUNNING, &data->flags);
			else
				btusb_submit_isoc_urb(hdev, GFP_KERNEL);   //TODO	
		}
	}else {
		clear_bit(BTUSB_ISOC_RUNNING, &data->flags);
		usb_kill_anchored_urbs(&data->isoc_anchor);
		
		__set_isoc_interface(hdev, 0);
		if (test_and_clear_bit(BTUSB_DID_ISO_RESUME, &data->flags))
			usb_autopm_put_interface(data->isoc ? data->isoc : data->intf);
	}
}
/**
 * usb_autopm_get_interface - increment a USB interface's PM-usage counter
 * @intf: the usb_interface whose counter should be incremented
 *
 * This routine should be called by an interface driver when it wants to
 * use @intf and needs to guarantee that it is not suspended.  In addition,
 * the routine prevents @intf from being autosuspended subsequently.  (Note
 * that this will not prevent suspend events originating in the PM core.)
 * This prevention will persist until usb_autopm_put_interface() is called
 * or @intf is unbound.  A typical example would be a character-device
 * driver when its device file is opened.
 *
 * @intf's usage counter is incremented to prevent subsequent autosuspends.
 * However if the autoresume fails then the counter is re-decremented.
 *
 * This routine can run only in process context.
 */

static void btusb_waker(struct work_struct *work)
{
	struct btusb_data *data = container_of(work, struct btusb_data, waker);
	int err;
	
	err = usb_autopm_get_interface(data->intf);
	if (err < 0)
		return;
	usb_autopm_put_interface(data->intf);
	
}
/*The Endpoint Descriptor (USB_ENDPOINT_DESCRIPTOR) specifies the transfer type, direction, 
  polling interval, and maximum packet size for each endpoint. Endpoint 0 (zero), the default 
  endpoint, is always assumed to be a control endpoint and never has a descriptor.
*/
/*
 * @driver_info: Holds information used by the driver.  Usually it holds
 *	a pointer to a descriptor understood by the driver, or perhaps
 *	device flags.
*/
/*
 * usb_match_id - find first usb_device_id matching device or interface
 * @interface: the interface of interest
 * @id: array of usb_device_id structures, terminated by zero entry
 *
 * usb_match_id searches an array of usb_device_id's and returns
 * the first one matching the device or interface, or null.
 * This is used when binding (or rebinding) a driver to an interface.
 * Most USB device drivers will use this indirectly, through the usb core,
 * but some layered driver frameworks use it directly.
 * These device tables are exported with MODULE_DEVICE_TABLE, through
 * modutils, to support the driver loading functionality of USB hotplugging.
 *
 * Return: The first matching usb_device_id, or %NULL.
 */
/**
 * usb_endpoint_is_int_in - check if the endpoint is interrupt IN
 * @epd: endpoint to be checked
 *
 * Returns true if the endpoint has interrupt transfer type and IN direction,
 * otherwise it returns false.
 */
/**
 * usb_endpoint_is_int_out - check if the endpoint is interrupt OUT
 * @epd: endpoint to be checked
 *
 * Returns true if the endpoint has interrupt transfer type and OUT direction,
 * otherwise it returns false.
 */
 /* A USB driver needs to support some callbacks requiringa driver to cease all 
    IO to an interface. To do so, a driver has to keep track of the URBs it has 
    submitted to know they've all completed or to call usb_kill_urb for them.
 */
/**
 * usb_ifnum_to_if - get the interface object with a given interface number
 * @dev: the device whose current configuration is considered
 * @ifnum: the desired interface
 *
 * This walks the device descriptor for the currently active configuration
 * and returns a pointer to the interface with that particular interface
 * number, or null.
 */
/*
 * set_bit - Atomically set a bit in memory
 * @nr: the bit to set
 * @addr: the address to start counting from
 *
 * This function is atomic and may not be reordered.  See __set_bit()
 * if you do not require the atomic guarantees.
 */
/**
 *	skb_queue_tail - queue a buffer at the list tail
 *	@list: list to use
 *	@newsk: buffer to queue
 *
 *	Queue a buffer at the tail of the list. This function takes the
 *	list lock and can be used safely with other locking &sk_buff functions
 *	safely.
 *
 *	A buffer cannot be placed on two lists at the same time.
 */
/*
 * usb_driver_claim_interface - bind a driver to an interface
 * @driver: the driver to be bound
 * @iface: the interface to which it will be bound; must be in the
 *	usb device's active configuration
 * @priv: driver data associated with that interface
 *
 * This is used by usb device drivers that need to claim more than one
 * interface on a device when probing (audio and acm are current examples).
 * No device driver should directly modify internal usb_interface or
 * usb_device structure members.
*/
static int btusb_probe(struct usb_interface *intf,
				const struct usb_device_id *id)
{
	struct usb_endpoint_descriptor *ep_desc;
	struct btusb_data *data;
	struct hci_dev *hdev;
	int i, err;

	BT_DBG("intf %p id %p", intf, id);

	/* interface numbers are hardcoded in the spec */
	if(intf->cur_altsetting->desc.bInterfaceNumber != 0)
		return -ENODEV;
	if(!id->driver_info){
		const struct usb_device_id *match;
		match = usb_match_id(intf, blacklist_table);
		if (match)
		    id = match;
	}
	if(id->driver_info ==  BTUSB_IGNORE )	/*No Such Device*/
		return -ENODEV;
	if (ignore_dga && id->driver_info & BTUSB_DIGIANSWER)
		return -ENODEV;
	if(ignore_csr && id->driver_info & BTUSB_CSR)
		return -ENODEV;
	if(ignore_sniffer && id->driver_info & BTUSB_SNIFFER)	
		return -ENODEV;
	/*Get the reference to usb_device by the reference to usb_interface*/
	if(id->driver_info & BTUSB_ATH3012){
		struct usb_device *udev = interface_to_usbdev(intf);
		/* Old firmware would otherwise let ath3k driver load
		 * patch and sysconfig files */ 
		if (le16_to_cpu(udev->descriptor.bcdDevice) <= 0x0001) /* Device Release Number,Should be gt 0001*/
			return -ENODEV; 	
	}
	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;	
	for (i = 0; i < intf->cur_altsetting->desc.bNumEndpoints; i++) { /* Number of endpoints used by this interface*/
		ep_desc = &intf->cur_altsetting->endpoint[i].desc;	
		if(!data->intr_ep && usb_endpoint_is_int_in(ep_desc)){
			data->intr_ep = ep_desc;
			continue;
		}
		if(!data->bulk_tx_ep && usb_endpoint_is_bulk_out(ep_desc)){
			data->bulk_tx_ep = ep_desc;
			continue;	
		}
		if (!data->bulk_rx_ep && usb_endpoint_is_bulk_in(ep_desc)) {
			data->bulk_rx_ep = ep_desc;
			continue;
		}
	}
	if(!data->intr_ep || !data->bulk_tx_ep || !data->bulk_rx_ep){
		kfree(data);
		return -ENODEV;
	}

	data->cmdreq_type = USB_TYPE_CLASS;

	data->udev = interface_to_usbdev(intf);
	data->intf = intf;
	
	/*Init part : initialize all of a work item in one go */
	spin_lock_init(&data->lock);

	INIT_WORK(&data->work, btusb_work);
	INIT_WORK(&data->waker, btusb_waker);
	spin_lock_init(&data->txlock);
	
	/* Allocation and Initialisation of anchor */
	init_usb_anchor(&data->tx_anchor);
	init_usb_anchor(&data->intr_anchor);
	init_usb_anchor(&data->bulk_anchor);
	init_usb_anchor(&data->isoc_anchor);
	init_usb_anchor(&data->deferred);
	
	/* Alloc HCI device */	
	hdev = hci_alloc_dev();
	if (!hdev) {
		kfree(data);
		return -ENOMEM;
	}
	/* HCI bus types */
	hdev->bus = HCI_USB;
	
	hci_set_drvdata(hdev, data);
	data->hdev = hdev;
	
	/*Set Device*/	
	SET_HCIDEV_DEV(hdev, &intf->dev);

	hdev->open     = btusb_open;
	hdev->close    = btusb_close;
	hdev->flush    = btusb_flush;
	hdev->send     = btusb_send_frame;
	hdev->notify   = btusb_notify;

	/* Interface numbers are hardcoded in the specification */
	data->isoc = usb_ifnum_to_if(data->udev, 1);  /*get the interface object with a given interface number */
	
	if (!reset)
		set_bit(HCI_QUIRK_NO_RESET, &hdev->quirks);

	if (force_scofix || id->driver_info & BTUSB_WRONG_SCO_MTU) {
		if (!disable_scofix)
			set_bit(HCI_QUIRK_FIXUP_BUFFER_SIZE, &hdev->quirks);
	}
	if (id->driver_info & BTUSB_BROKEN_ISOC)
		data->isoc = NULL;
	if (id->driver_info & BTUSB_DIGIANSWER) {
		data->cmdreq_type = USB_TYPE_VENDOR;
		set_bit(HCI_QUIRK_NO_RESET, &hdev->quirks);
	}
	
	if (id->driver_info & BTUSB_CSR) {
		struct usb_device *udev = data->udev;

		/* Old firmware would otherwise execute USB reset */
		if (le16_to_cpu(udev->descriptor.bcdDevice) < 0x117)
			set_bit(HCI_QUIRK_NO_RESET, &hdev->quirks);
	}

	if (id->driver_info & BTUSB_SNIFFER) {
		struct usb_device *udev = data->udev;

		/* New sniffer firmware has crippled HCI interface */
		if (le16_to_cpu(udev->descriptor.bcdDevice) > 0x997)
			set_bit(HCI_QUIRK_RAW_DEVICE, &hdev->quirks);

		data->isoc = NULL;
	}

	if (id->driver_info & BTUSB_BCM92035) {
		unsigned char cmd[] = { 0x3b, 0xfc, 0x01, 0x00 };
		struct sk_buff *skb;
		/*allocate buffer for bt device*/
		skb = bt_skb_alloc(sizeof(cmd), GFP_KERNEL);
		if (skb) {
			memcpy(skb_put(skb, sizeof(cmd)), cmd, sizeof(cmd));   /* Add data to an sk_buff*/
			skb_queue_tail(&hdev->driver_init, skb);
		}
	}
	
	if (data->isoc) {
		/* used these for multi-interface device registration */
		err = usb_driver_claim_interface(&btusb_driver,			/*Bind a driver to an interface*/
							data->isoc, data);
		if (err < 0) {
			hci_free_dev(hdev);
			kfree(data);
			return err;
		}
	}
	/* Register HCI device */
	err = hci_register_dev(hdev);
	if(err < 0){
		hci_free_dev(hdev);
		kfree(data);
		return err;
	}
	
	usb_set_intfdata(intf, data); /*Setting  driver data */

	return 0;
}
static void btusb_disconnect(struct usb_interface *intf)
{
	struct btusb_data *data = usb_get_intfdata(intf); /*Accessing the bt_data*/
	struct hci_dev *hdev;
	
	BT_DBG("intf %p", intf);
	
	if (!data)
		return;
	hdev = data->hdev;
	usb_set_intfdata(data->intf, NULL);
	
	if (data->isoc)
		usb_set_intfdata(data->isoc, NULL);	

	hci_unregister_dev(hdev);
	
	if (intf == data->isoc)
		usb_driver_release_interface(&btusb_driver, data->intf);
	else if (data->isoc)
		usb_driver_release_interface(&btusb_driver, data->isoc);

	hci_free_dev(hdev);
	kfree(data);
}
/*
@supports_autosuspend: if set to 0, the USB core will not allow autosuspend
  			 for interfaces bound to this driver.
*/
static struct usb_driver btusb_driver = {
	.name		= "btusb",
	.probe		= btusb_probe,
	.disconnect	= btusb_disconnect,
	.id_table	= btusb_table,
	.supports_autosuspend = 1,
};

/**
 * module_usb_driver() - Helper macro for registering a USB driver
 * @__usb_driver: usb_driver struct
 *
 * Helper macro for USB drivers which do not do anything special in module
 * init/exit. This eliminates a lot of boilerplate. Each module may only
 * use this macro once, and calling it replaces module_init() and module_exit()
 */
module_usb_driver(btusb_driver);

module_param(ignore_dga, bool, 0644);
MODULE_PARM_DESC(ignore_dga, "Ignore devices with id 08fd:0001");

module_param(ignore_csr, bool, 0644);
MODULE_PARM_DESC(ignore_csr, "Ignore devices with id 0a12:0001");

module_param(ignore_sniffer, bool, 0644);
MODULE_PARM_DESC(ignore_sniffer, "Ignore devices with id 0a12:0002");

module_param(disable_scofix, bool, 0644);
MODULE_PARM_DESC(disable_scofix, "Disable fixup of wrong SCO buffer size");

module_param(force_scofix, bool, 0644);
MODULE_PARM_DESC(force_scofix, "Force fixup of wrong SCO buffers size");

module_param(reset, bool, 0644);
MODULE_PARM_DESC(reset, "Send HCI reset command on initialization");

MODULE_AUTHOR("Chandan Jha <beingchandanjha@gmail.com>");
MODULE_DESCRIPTION("Generic Bluetooth USB driver : Rewritten  for better understanding of code"");
MODULE_VERSION(".1");
MODULE_LICENSE("GPL");
