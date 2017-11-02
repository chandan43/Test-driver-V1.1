#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/usb/input.h>
#include <linux/hid.h>

struct usb_mouse{
	char name[128];
	char phys[64];
	struct usb_device *usbdev;
	struct input_dev *dev;
	struct urb *irq;

	signed char *data;
	dma_addr_t data_dma;
};

/*
 * The context field is normally used to link URBs back to the relevant
 * driver or request state.
 */
/*
	Byte   						Bits    					   Description
	0                                                 0                                                   Button                                                 
	1                                                 1                                                   Button                                                   
	2                                                 2                                                   Button    
	1 						0 to 7 						X displacement    
	2 						0 to 7 						Y displacement  
	3 to n 						0 to 7 						Device specific (optional)                                      
*/

static void usb_mouse_irq(struct urb *urb){
	struct usb_mouse *mouse = urb->context;
	signed char *data = mouse->data;           /*Backup*/
	struct input_dev *dev = mouse->dev;
	int status;
	switch (urb->status) {	
		case 0:			/* success */
			break;
		case -ECONNRESET:	/* unlink ,Connection reset by peer */
		case -ENOENT:   	/*No such device*/ 
		case -ESHUTDOWN:	/*Can't send after transport endpoint shutdown*/
			return;
		/* -EPIPE:  should clear the halt */
		default:		/* error */
			goto resubmit;	
	}
	/*which upon every interrupt from the button checks its state and reports it via the input_report_key() call to the input system. */
	input_report_key(dev, BTN_LEFT,   data[0] & 0x01);
	input_report_key(dev, BTN_RIGHT,  data[0] & 0x02);
	input_report_key(dev, BTN_MIDDLE, data[0] & 0x04);
	input_report_key(dev, BTN_SIDE,   data[0] & 0x08);
	input_report_key(dev, BTN_EXTRA,  data[0] & 0x10);
	
	input_report_rel(dev, REL_X,     data[1]);
	input_report_rel(dev, REL_Y,     data[2]);
	input_report_rel(dev, REL_WHEEL, data[3]);
	
	//Tells the input systems the we are done sending data.
	input_sync(dev);
resubmit:
	status = usb_submit_urb (urb, GFP_ATOMIC);
	if (status)
		dev_err(&mouse->usbdev->dev,"can't resubmit intr, %s-%s/input0, status %d\n",mouse->usbdev->bus->bus_name,mouse->usbdev->devpath, status);
}

/**
 * usb_submit_urb - issue an asynchronous transfer request for an endpoint
 * @urb: pointer to the urb describing the request
 * @mem_flags: the type of memory to allocate, see kmalloc() for a list
 *	of valid options for this.
 */
static int usb_mouse_open(struct input_dev *dev)
{
	struct usb_mouse *mouse = input_get_drvdata(dev);
	mouse->irq->dev = mouse->usbdev;
	if(usb_submit_urb(mouse->irq, GFP_KERNEL)) 		 /* Return:* 0 on successful submissions. A negative error number otherwise.*/ 
		return -EIO;
	return 0;
}
static void usb_mouse_close(struct input_dev *dev)
{
	struct usb_mouse *mouse = input_get_drvdata(dev);
	usb_kill_urb(mouse->irq);                             	/*Stop request*/
}

/**
 * usb_alloc_coherent - allocate dma-consistent buffer for URB_NO_xxx_DMA_MAP
 * @dev: device the buffer will be used with
 * @size: requested buffer size
 * @mem_flags: affect whether allocation may block
 * @dma: used to return DMA address of buffer
 *
 * Return: Either null (indicating no buffer could be allocated), or the
 * cpu-space pointer to a buffer that may be used to perform DMA to the
 * specified device.  Such cpu-space buffers are returned along with the DMA
 * address (through the pointer provided).
 */
/**
 * usb_alloc_urb - creates a new urb for a USB driver to use
 * @iso_packets: number of iso packets for this urb
 * @mem_flags: the type of memory to allocate, see kmalloc() for a list of
 *	valid options for this.
 */
 /* Event types: input-event-codes.h
#define EV_SYN			0x00     Used as markers to separate events. Events may be separated in time or in space, such as with the multitouch protocol.
#define EV_KEY			0x01     Used to describe state changes of keyboards, buttons, or other key-like devices.
#define EV_REL			0x02     Used to describe relative axis value changes, e.g. moving the mouse 5 units to the left.        
#define EV_ABS			0x03     Used to describe absolute axis value changes, e.g. describing the coordinates of a touch on a touchscreen.
#define EV_MSC			0x04     Used to describe miscellaneous input data that do not fit into other types.
#define EV_SW			0x05     Used to describe binary state input switches.
#define EV_LED			0x11     Used to turn LEDs on devices on and off
#define EV_SND			0x12     Used to output sound to devices.
#define EV_REP			0x14     Used for autorepeating devices.
#define EV_FF			0x15     Used to send force feedback commands to an input device.
#define EV_PWR			0x16     A special type for power button and switch input.
#define EV_FF_STATUS		0x17     Used to receive force feedback device status.
#define EV_MAX			0x1f
#define EV_CNT			(EV_MAX+1) */
/**
 * usb_fill_int_urb - macro to help initialize a interrupt urb
 * @urb: pointer to the urb to initialize.
 * @dev: pointer to the struct usb_device for this urb.
 * @pipe: the endpoint pipe
 * @transfer_buffer: pointer to the transfer buffer
 * @buffer_length: length of the transfer buffer
 * @complete_fn: pointer to the usb_complete_t function
 * @context: what to set the urb context to.
 * @interval: what to set the urb interval to, encoded like
 *	the endpoint descriptor's bInterval value.
 *
 * Initializes a interrupt urb with the proper information needed to submit
 * it to a device.
*/
static int usb_mouse_probe(struct usb_interface *intf,const struct usb_device_id *id)
{
	struct usb_device *dev= interface_to_usbdev(intf);        /* Convert data from a given struct usb_interface structure into struct usb_device structure*/
	struct usb_host_interface *interface;
	struct usb_endpoint_descriptor *endpoint;                 /* Contains all of the USB-specific data in the exact format that the device itself specified.*/
	struct usb_mouse *mouse;                                  /* struct usb_mouse - state of each attached mouse*/
	struct input_dev *input_dev;
	int pipe, maxp;
	int error = -ENOMEM;
	interface = intf->cur_altsetting;			/*Currently active a Number of endpoint*/
	if (interface->desc.bNumEndpoints != 1)               	/*Number of endpoints used by this interface*/
		return -ENODEV;                                 /*No Such Device */
	endpoint = &interface->endpoint[0].desc;		
 	if(!usb_endpoint_is_int_in(endpoint))                  /* check if the endpoint is interrupt IN,Returns true if the endpoint has interrupt transfer type and IN direction, */  
		return -ENODEV;
	/*@pipe: Holds endpoint number, direction, type, and more*/
        pipe=usb_rcvintpipe(dev, endpoint->bEndpointAddress); /*#define usb_rcvintpipe(dev, endpoint).bEndpointAddress: The address of the endpoint described by this descriptor. */
	/*usb_maxpacket(struct usb_device *udev, int pipe, int is_out)*/
	maxp=usb_maxpacket(dev, pipe, usb_pipeout(pipe));     /* get endpoint's max packet size, */
	mouse= kzalloc(sizeof(struct usb_mouse), GFP_KERNEL);
/**
 * input_allocate_device - allocate memory for new input device
 *
 * Returns prepared struct input_dev or %NULL.
 *
 * NOTE: Use input_free_device() to free devices that have not been
 * registered; input_unregister_device() should be used for already
 * registered devices.
 */
	input_dev = input_allocate_device();
	if (!mouse || !input_dev)
		goto fail1;
	mouse->data = usb_alloc_coherent(dev, 8, GFP_ATOMIC, &mouse->data_dma);
	if (!mouse->data)
		goto fail1;
	mouse->irq = usb_alloc_urb(0, GFP_KERNEL);
	if(!mouse->irq)
		goto fail2;
	mouse->usbdev = dev;
	mouse->dev = input_dev;
	if (dev->manufacturer)
		strlcpy(mouse->name, dev->manufacturer, sizeof(mouse->name));

	if (dev->product) {
		if (dev->manufacturer)
			strlcat(mouse->name, " ", sizeof(mouse->name));
		strlcat(mouse->name, dev->product, sizeof(mouse->name));
	}
	if (!strlen(mouse->name))
		snprintf(mouse->name, sizeof(mouse->name), "USB HIDBP Mouse %04x:%04x",le16_to_cpu(dev->descriptor.idVendor),le16_to_cpu(dev->descriptor.idProduct));
	
/**
 * usb_make_path - returns stable device path in the usb tree
 * @dev: the device whose path is being constructed
 * @buf: where to put the string
 * @size: how big is "buf"?
 *
 * Return: Length of the string (> 0) or negative if size was too small.
 */	
	usb_make_path(dev, mouse->phys, sizeof(mouse->phys));
	strlcat(mouse->phys, "/input0", sizeof(mouse->phys));
	input_dev->name = mouse->name;
	input_dev->phys = mouse->phys;
	
	/*Populate the input_id structure with information from usb dev's descriptor*/
	usb_to_input_id(dev, &input_dev->id);
	input_dev->dev.parent = &intf->dev;
	/*
		BIT_WORD(x)	 - returns the index in the array in longs for bit x
		BIT_MASK(x)	 - returns the index in a long for bit x
	*/
	/*@evbit: bitmap of types of events supported by the device (EV_KEY, EV_REL, etc.)*/
	input_dev->evbit[0]= BIT_MASK(EV_KEY) | BIT_MASK(EV_REL);            /* EV_REL: Used to describe relative axis value changes, e.g. moving the mouse 5 units to the left.*/
	/*@keybit: bitmap of keys/buttons this device has :[BTN_LEFT] = "LeftBtn",[BTN_RIGHT] = "RightBtn", [BTN_MIDDLE] = "MiddleBtn", [BTN_EXTRA] = "ExtraBtn" [BTN_SIDE] = "SideBtn",*/
	input_dev->keybit[BIT_WORD(BTN_MOUSE)] = BIT_MASK(BTN_LEFT) |BIT_MASK(BTN_RIGHT) | BIT_MASK(BTN_MIDDLE);  /*Discribed on /include/uapi/linux/input.h*/
	input_dev->relbit[0] = BIT_MASK(REL_X) | BIT_MASK(REL_Y);   /* @relbit: bitmap of relative axes for the device*/
	input_dev->keybit[BIT_WORD(BTN_MOUSE)] |= BIT_MASK(BTN_SIDE) |BIT_MASK(BTN_EXTRA);
	input_dev->relbit[0] |= BIT_MASK(REL_WHEEL);
			
	input_set_drvdata(input_dev, mouse);
	
	input_dev->open = usb_mouse_open;
	input_dev->close = usb_mouse_close;
	/*initialize a interrupt urb*/
	usb_fill_int_urb(mouse->irq, dev, pipe, mouse->data,(maxp > 8 ? 8 : maxp),usb_mouse_irq, mouse, endpoint->bInterval);
	
	mouse->irq->transfer_dma = mouse->data_dma;
	/*set URB_NO_TRANSFER_DMA_MAP so that usbcore won't map or unmap the buffer.  They cannot be used for setup_packet buffers in control requests.*/
	mouse->irq->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
	
	/*Register input device*/
	error = input_register_device(mouse->dev);
	if (error)
		goto fail3;
	
	/*Because the USB driver needs to retrieve the local data structure that is associated with this struct usb_interface 
	later in the lifecycle of the device, the function usb_set_intfdata can be called:*/
	usb_set_intfdata(intf, mouse);		
	pr_info("%s: Probe invoked\n",__func__);
	return 0;

fail3:
	usb_free_urb(mouse->irq);
fail2:
	usb_free_coherent(dev, 8, mouse->data, mouse->data_dma);
fail1:
	input_free_device(input_dev);
	kfree(mouse);
	return error;
}
static void usb_mouse_disconnect(struct usb_interface *intf)
{	
	struct usb_mouse *mouse = usb_get_intfdata (intf);
	usb_set_intfdata(intf, NULL);
	if (mouse) {
		usb_kill_urb(mouse->irq);		/** usb_kill_urb - cancel a transfer request and wait for it to finish*/
		input_unregister_device(mouse->dev);   /*unregister input device*/
		usb_free_urb(mouse->irq);	      /* usb_free_urb - frees the memory used by a urb when all users of it are finished*/
		usb_free_coherent(interface_to_usbdev(intf), 8, mouse->data, mouse->data_dma);  /*When the buffer is no longer used, free it with usb_free_coherent.*/
		kfree(mouse);
	}
	pr_info("%s: Disconnect invoked\n",__func__);
} 
static const struct usb_device_id usb_mouse_id_table[] ={
	{ USB_INTERFACE_INFO(USB_INTERFACE_CLASS_HID, USB_INTERFACE_SUBCLASS_BOOT,
		USB_INTERFACE_PROTOCOL_MOUSE) },
	{ }	/* Terminating entry */
};
MODULE_DEVICE_TABLE (usb, usb_mouse_id_table);

static struct usb_driver  usb_mouse_driver ={
	.name		= "usbmouse",
	.probe		= usb_mouse_probe,
	.disconnect	= usb_mouse_disconnect,
	.id_table	= usb_mouse_id_table,
};

module_usb_driver(usb_mouse_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("USB mouse driver");
MODULE_VERSION(".1");
MODULE_AUTHOR("Chandan Jha :beingchandanjha@gmail.com");
