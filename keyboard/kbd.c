/* This driver is based on the 2.6.3 version of drivers/hid/usbhid/usbkbd.c
 * but has been rewritten to be easier to read and use
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/usb/input.h>
#include <linux/hid.h>

/*
 * Conversion between standard USB scancodes and Linux HID core codes.
*/
static const unsigned char usb_kbd_keycode[256] = {
	  0,  0,  0,  0, 30, 48, 46, 32, 18, 33, 34, 35, 23, 36, 37, 38,
	 50, 49, 24, 25, 16, 19, 31, 20, 22, 47, 17, 45, 21, 44,  2,  3,
	  4,  5,  6,  7,  8,  9, 10, 11, 28,  1, 14, 15, 57, 12, 13, 26,
	 27, 43, 43, 39, 40, 41, 51, 52, 53, 58, 59, 60, 61, 62, 63, 64,
	 65, 66, 67, 68, 87, 88, 99, 70,119,110,102,104,111,107,109,106,
	105,108,103, 69, 98, 55, 74, 78, 96, 79, 80, 81, 75, 76, 77, 71,
	 72, 73, 82, 83, 86,127,116,117,183,184,185,186,187,188,189,190,
	191,192,193,194,134,138,130,132,128,129,131,137,133,135,136,113,
	115,114,  0,  0,  0,121,  0, 89, 93,124, 92, 94, 95,  0,  0,  0,
	122,123, 90, 91, 85,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	 29, 42, 56,125, 97, 54,100,126,164,166,165,163,161,115,114,113,
	150,158,159,128,136,177,178,176,142,152,173,140
};

/**
 * struct usb_kbd - state of each attached keyboard
 * @dev:	input device associated with this keyboard
 * @usbdev:	usb device associated with this keyboard
 * @old:	data received in the past from the @irq URB representing which
 *		keys were pressed. By comparing with the current list of keys
 *		that are pressed, we are able to see key releases.
 * @irq:	URB for receiving a list of keys that are pressed when a
 *		new key is pressed or a key that was pressed is released.
 * @led:	URB for sending LEDs (e.g. numlock, ...)
 * @newleds:	data that will be sent with the @led URB representing which LEDs
 		should be on
 * @name:	Name of the keyboard. @dev's name field points to this buffer
 * @phys:	Physical path of the keyboard. @dev's phys field points to this
 *		buffer
 * @new:	Buffer for the @irq URB
 * @cr:		Control request for @led URB
 * @leds:	Buffer for the @led URB
 * @new_dma:	DMA address for @irq URB
 * @leds_dma:	DMA address for @led URB
 * @leds_lock:	spinlock that protects @leds, @newleds, and @led_urb_submitted
 * @led_urb_submitted: indicates whether @led is in progress, i.e. it has been
 *		submitted and its completion handler has not returned yet
 *		without	resubmitting @led
 */
static struct usb_kbd {
	struct input_dev *dev;
	struct usb_device *usbdev;
	unsigned char old[8];
	struct urb *irq, *led;
	unsigned char newleds;
	char name[128];
	char phys[64];
	unsigned char *new;
	struct usb_ctrlrequest *cr;
	unsigned char *leds;
	dma_addr_t new_dma
	dma_addr_t leds_dma;
	spinlock_t leds_lock;
	bool led_urb_submitted;
};
/*
Byte  Description 
0      Modifier       keys                                                                          
1      Reserved       
2      Keycode       1                                                                          
3      Keycode       2                                                                          
4      Keycode       3                                                                          
5      Keycode       4                                                                          
6      Keycode       5                                                                          
7      Keycode       6                                                                          
Byte 1 of this report is a constant. This byte is reserved for OEM use. The 
BIOS should ignore this field if it is not used. Returning zeros in unused fields is 
recommended. */
/*
 * Interrupt handler that receives a USB Request Block (URB) when the bound device interrupts.
 * Key press data for this device is found in urb->context->new[2+]
 */
static void usb_kbd_irq(struct urb *urb){
	struct usb_kbd *kbd = urb->context;			/*(in) context for completion */
	int i;
	/* (return) non-ISO status */
	switch(urb->status){
		case 0:			/* success */
			break;
		case -ECONNRESET:	/* unlink ,Connection reset by peer */
		case -ENOENT:           /*No such device*/
		case -ESHUTDOWN:        /*Can't send after transport endpoint shutdown*/
			return;
		/* -EPIPE:  should clear the halt */
		default:		/* error */
			goto resubmit;
	}
	/*which upon every interrupt from the button checks its state and reports it via the input_report_key() call to the input system. */
	/* input_event() - report new input event input_report_key(struct input_dev *dev, unsigned int code, int value)
	
	224 	225 	226 	227 	228 	229 	230 	231
	LCtrl 	LShift 	LAlt 	LGUI 	RCtrl 	RShift 	RAlt 	RGUI 
	*/
	for(i=0;i<8;i++)
		input_report_key(kbd->dev, usb_kbd_keycode[i + 224], (kbd->new[0] >> i) & 1); /*224 + 8 character*/
	//kbd->old[i] > 3 and kbd->new 2-7 does not contain kbd->old[i]
        //if a scancode is in old and not in new
        //RELEASE
	/* memscan --  Find a character in an area of memory,returns the address of the first occurrence of c, or 1 byte past the area if c is not found */
	for(i=2;i<8;i++){
		if(kbd->old[i] > 3 && memscan(kbd->new + 2, kbd->old[i], 6) == kbd->new + 8){	
			if(usb_kbd_keycode[kbd->old[i]])
				input_report_key(kbd->dev, usb_kbd_keycode[kbd->old[i]], 0);  //release
			else
				hid_info(urb->dev,
					 "Unknown key (scancode %#x) released.\n",
					 kbd->old[i]);
		}
	//kbd->new[i] > 3 and kbc->old 2-7 does not contain kbd->new[i]
        //if a scancode is in new and not in old
        //PRESS
		if(kbd->new[i] > 3 && memscan(kbd->old + 2, kbd->new[i], 6) == kbd->old + 8) {
				if (usb_kbd_keycode[kbd->new[i]])
					input_report_key(kbd->dev, usb_kbd_keycode[kbd->new[i]], 1); //input
				else
					hid_info(urb->dev,
					 	"Unknown key (scancode %#x) pressed.\n",
					 	kbd->new[i]);
		}
	}
	//Tells the input systems the we are done sending data.
	input_sync(kbd->dev);
	//Copy data from new buffer to old buffer.  
   	//Needed to compare previous state and register key releases.
	memcpy(kbd->old, kbd->new, 8);
resubmit:
	i= usb_submit_urb(urb,GFP_ATOMIC);
	if(i)
		hid_err(urb->dev, "can't resubmit intr, %s-%s/input0, status %d",
			kbd->usbdev->bus->bus_name,
			kbd->usbdev->devpath, i);
}
/*We have already seen how spin_lock works. spin_lock_irqsave disables interrupts (on the local processor only) 
	before taking the spinlock; the previous interrupt state is stored in flags. */
static int usb_kbd_event(struct input_dev *dev, unsigned int type, unsigned int code, int value){
	unsigned long flags;
	struct usb_kbd *kbd=input_get_drvdata(dev);
	if (type != EV_LED)
		return -1;
	spin_lock_irqsave(&kbd->leds_lock, flags);	
/**
 * test_bit - Determine whether a bit is set
 * @nr: bit number to test
 * @addr: Address to start counting from
 * EX =0x04 & 0x0F = 0x04 (4) 
 * !!(0x04 & 0x0F) = !4 = ! 0 = 1 
 */
	kbd->newleds = 	(!!test_bit(LED_KANA, dev->led) << 3) | (!!(test_bit(LED_COMPOSE, dev->led) << 3))
		        (!!test_bit(LED_SCROLLL, dev->led) << 2) | (!!test_bit(LED_CAPSL, dev->led) << 1) |
			(!!test_bit(LED_NUML, dev->led));

}
static void usb_kbd_led(struct urb *urb){
	unsigned long flags;
	struct usb_kbd *kbd = urb->context;
	if(urb->status) ///* (return) non-ISO status */
		hid_warn(urb->dev, "led urb status %d received\n",
					 urb->status);
	spin_lock_irqsave(&kbd->leds_lock, flags);
       if*(kbd->leds)==

}

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
static  int usb_kbd_open(struct input_dev *dev){
	struct usb_kbd *kbd=input_get_drvdata(dev);
	kbd->irq->dev=kbd->usbdev;                  /*(in) pointer to associated device */
	if(usb_submit_urb(kbd->irq,GFP_KERNEL))            /* Return:* 0 on successful submissions. A negative error number otherwise.*/
		return -EIO;
	return 0;
}
/**
 * usb_kill_urb - cancel a transfer request and wait for it to finish
 * @urb: pointer to URB describing a previously submitted request,
 *	may be NULL
 */

static void usb_kbd_close(struct input_dev *dev)
{
	struct usb_kbd *kbd = input_get_drvdata(dev);

	usb_kill_urb(kbd->irq);
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
 *
 * Note:
 * These buffers are used with URB_NO_xxx_DMA_MAP set in urb->transfer_flags
 * to avoid behaviors like using "DMA bounce buffers", or thrashing IOMMU
 * hardware during URB completion/resubmit.  The implementation varies between
 * platforms, depending on details of how DMA will work to this device.
 * Using these buffers also eliminates cacheline sharing problems on
 * architectures where CPU caches are not DMA-coherent.  On systems without
 * bus-snooping caches, these buffers are uncached.
 *
 * When the buffer is no longer used, free it with usb_free_coherent().
 */
static int  usb_kbd_alloc_mem(struct usb_device *dev, struct usb_kbd *kbd){
	if(!(kbd->irq = usb_alloc_urb(0, GFP_KERNEL)))
		return -1;
	if(!(kbd->led = usb_alloc_urb(0, GFP_KERNEL) ))
		return -1;
	if(!(kbd->new = usb_alloc_coherent(dev,8, GFP_ATOMIC, &kbd->new_dma)))
		return -1;
	if(!(kbd->cr = kmalloc(sizeof(struct usb_ctrlrequest), GFP_KERNEL)))
		return -1;
	if(!(kbd->leds = usb_alloc_coherent(dev,1, GFP_ATOMIC, &kbd->leds_dma))	
		retuen -1;
}
static void  usb_kbd_free_mem(struct usb_device *dev, struct usb_kbd *kbd){
	usb_free_urb(kbd->irq);
	usb_free_urb(kbd->led);
	usb_free_coherent(dev, 8, kbd->new, kbd->new_dma);
	kfree(kbd->cr)
	usb_free_coherent(dev,1, kbd->leds, kbd->leds_dma);
} 

/* interface_to_usbdev :- A USB device driver commonly has to convert data from a given struct usb_interface structure 
   into a struct usb_device structure that the USB core needs for a wide range of function calls.
 * usb_host_interface: -USB endpoints are bundled up into interfaces. USB interfaces handle only one type of a USB 
  logical connection, such as a mouse, a keyboard, or a audio stream.such as a USB speaker that might consist of two 
  interfaces: a USB keyboard for the buttons and a USB audio stream. USB interfaces are described in the kernel with 
  the struct usb_interface structure. This structure is what the USB core passes to USB drivers and is what the USB 
  driver then is in charge of controlling. 
 *usb_endpoint_descriptor :The most basic form of USB communication is through something called an endpoint. A USB 
  endpoint can carry data in only one direction, either from the host computer to the device (called an OUT endpoint) 
  or from the device to the host computer (called an IN endpoint). Endpoints can be thought of as unidirectional pipes.   
  NOTE : Control and bulk endpoints are used for asynchronous data transfers, Interrupt and isochronous endpoints are periodic. 
  -->USB endpoints are described in the kernel with the structure struct usb_host_endpoint. This structure contains the real endpoint
  information in another structure called struct usb_endpoint_descriptor. The latter structure contains all of the USB-specific data 
  in the exact format that the device itself specified. The fields of this structure that drivers care about are:
   */
/*
 * Event types: input-event-codes.h
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

static int usb_kbd_probe(struct usb_interface *iface,const struct usb_device_id *id){
	pr_info("%s: Invoked Probe function\n",__func__);
	struct usb_device *dev=interface_to_usbdev(iface);     /* Convert data from a given struct usb_interface structure into struct usb_device structure*/
	struct usb_host_interface *interface;                  
	struct usb_endpoint_descriptor *endpoint;              /* Contains all of the USB-specific data in the exact format that the device itself specified.*/
	struct usb_kbd *kbd;                                   /* struct usb_kbd - state of each attached keyboard*/
	struct input_dev *input_dev;                           /* input device associated with this keyboard*/
	int i, pipe, maxp;
        int error = -ENOMEM;
	interface=iface->cur_altsetting;			/*Currently active alternate setting */
	if(interface->desc.bNumEndpoints !=1)			/*Number of endpoints used by this interface*/	
	          return -ENODEV;                               /*No Such Device */
	endpoint=&interface->endpoint[0].desc; 
 	if(!usb_endpoint_is_int_in(endpoint))                  /* check if the endpoint is interrupt IN,Returns true if the endpoint has interrupt transfer type and IN direction, */  
		return -ENODEV;
	/*@pipe: Holds endpoint number, direction, type, and more*/
        pipe=usb_rcvintpipe(dev, endpoint->bEndpointAddress); /*#define usb_rcvintpipe(dev, endpoint).bEndpointAddress: The address of the endpoint described by this descriptor. */
	/*usb_maxpacket(struct usb_device *udev, int pipe, int is_out)*/
	maxp=usb_maxpacket(dev, pipe, usb_pipeout(pipe));     /* get endpoint's max packet size, */
	kbd = kzalloc(sizeof(struct usb_kbd), GFP_KERNEL);
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
	if(!kbd || !input_dev)
                 goto fail1;
	if (usb_kbd_alloc_mem(dev, kbd))
		 goto fail2;
	kbd->usbdev=dev;
	spin_lock_init(&kbd->leds_lock);
	if(dev->manufacturer)
		strlcpy(kbd->name, dev->manufacturer, sizeof(kbd->name));
	if(dev->product){
		if(dev->manufacturer)
			strlcat(kbd->name, " ", sizeof(kbd->name));
`		strlcat(kbd->name, dev->product, sizeof(kbd->name));
	}	
	/*le16_to_cpu: To convert from little-endian format into the processor's native format you should use these functions*/
	if(!strlen(dev->name))
		snprintf(kbd->name, sizeof(kbd->name), "USB HIDBP Keyboard %04x:%04x", le16_to_cpu(dev->descriptor.idVendor), le16_to_cpu(dev->descriptor.idProduct)),
/**
 * usb_make_path - returns stable device path in the usb tree
 * @dev: the device whose path is being constructed
 * @buf: where to put the string
 * @size: how big is "buf"?
 *
 * Return: Length of the string (> 0) or negative if size was too small.
 */	
	usb_make_path(dev, kbd->phys, sizeof(kbd->phys));	
	strlcat(kbd->phys, "/input0", sizeof(kbd->phys));
	input_dev->name=kbd->name;
	input_dev->phys = kbd->phys;
	/*Populate the input_id structure with information from usb dev's descriptor*/
	usb_to_input_id(dev, &input_dev->id);
	input_dev->dev.parent= &iface->dev;
	input_set_drvdata(input_dev,kbd);
	/*@evbit: bitmap of types of events supported by the device (EV_KEY, EV_REL, etc.)*/
	input_dev->evbit[0]= BIT_MASK(EV_KEY) | BIT_MASK((EV_LED) | BIT_MASK(EV_REP);
	/*@ledbit: bitmap of leds present on the device : Num Lock | Caps Lock | Scroll Lock|Compose|Kanai*/
	input_dev->ledbit[0]= BIT_MASK(LED_NUML)|BIT_MASK(LED_CAPSL)|BIT_MASK(LED_SCROLLL)| BIT_MASK(LED_COMPOSE)|BIT_MASK(LED_KANA); 
	/*static inline void set_bit(int nr, void *addr)*/
/*
 * __set_bit - Set a bit in memory
 * @nr: the bit to set
 * @addr: the address to start counting from
 */
/*
 * clear_bit - Clears a bit in memory
 * @nr: Bit to clear
 * @addr: Address to start counting from
 */
	for(i=0;i<255;i++) 
		set_bit(usb_kbd_keycode[i],input_dev->keybit); 	
	clear_bit(0, input_dev->keybit);
	input_dev->event = usb_kbd_event;
	input_dev->open = usb_kbd_open;
	input_dev->close = usb_kbd_close;
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
	usb_fill_int_urb(kbd->irq, dev, pipe,
			 kbd->new, (maxp > 8 ? 8 : maxp),
			 usb_kbd_irq, kbd, endpoint->bInterval); /*The bInterval value contains the polling interval for interrupt and isochronous endpoints*/
	kbd->irq->transfer_dma = kbd->new_dma;
	kbd->irq->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;     /* urb->transfer_dma valid on submit --(in) URB_SHORT_NOT_OK | */
/**
 * struct usb_ctrlrequest - SETUP data for a USB device control request
 * @bRequestType: matches the USB bmRequestType field
 * @bRequest: matches the USB bRequest field
 * @wValue: matches the USB wValue field (le16 byte order)
 * @wIndex: matches the USB wIndex field (le16 byte order)
 * @wLength: matches the USB wLength field (le16 byte order)
 */
	/*https://www.pjrc.com/teensy/beta/usb20.pdf -pg 250*/
	kbd->cr->bRequestType = USB_TYPE_CLASS | USB_RECIP_INTERFACE;
	kbd->cr->bRequest= 0x09;             //set configuration
	kbd->cr->wValue = cpu_to_le16(0x200);
	kbd->cr->wIndex = cpu_to_le16(interface->desc.bInterfaceNumber);
	kbd->cr->wLength = cpu_to_le16(1);
/**
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
	usb_fill_control_urb(kbd->led, dev, usb_sndctrlpipe(dev, 0),
			     (void *) kbd->cr, kbd->leds, 1,
			     usb_kbd_led, kbd);
	kbd->led->transfer_dma = kbd->leds_dma;
	kbd->led->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

/**
 * input_register_device - register device with input core
 * @dev: device to be registered
 *
 * This function registers device with input core. The device must be
 * allocated with input_allocate_device() and all it's capabilities
 * set up before registering.
 * If function fails the device must be freed with input_free_device().
 * Once device has been successfully registered it can be unregistered
 * with input_unregister_device(); input_free_device() should not be
 * called in this case.
*/	
	error = input_register_device(kbd->dev);
	if(error)
		goto fail2;
	/*Because the USB driver needs to retrieve the local data structure that is associated with this struct usb_interface 
	later in the lifecycle of the device, the function usb_set_intfdata can be called:*/
	usb_set_intfdata(iface, kbd);  // Set interface data;
/**
 * device_set_wakeup_enable - Enable or disable a device to wake up the system.
 * @dev: Device to handle.
 */
	device_set_wakeup_enable(&dev->dev, 1);
	return 0;
fail2:
	usb_kbd_free_mem(dev, kbd);
fail1:	
        input_free_device(input_dev);
	kfree(kbd);
	return error;
} 

static void usb_kbd_disconnect(struct usb_interface *intf){
	pr_info("%s: Invoked Disconnect function\n",__func__);
}
/**
 * USB_INTERFACE_INFO - macro used to describe a class of usb interfaces
 * @cl: bInterfaceClass value
 * @sc: bInterfaceSubClass value
 * @pr: bInterfaceProtocol value
 *
 * This macro is used to create a struct usb_device_id that matches a
 * specific class of interfaces.
 */

/* table of devices that work with this driver */
static struct usb_device_id usb_kbd_id_table[]={
	{	USB_INTERFACE_INFO(USB_INTERFACE_CLASS_HID, USB_INTERFACE_SUBCLASS_BOOT, 
					USB_INTERFACE_PROTOCOL_KEYBOARD)},
	{ }
};

/* Export this with MODULE_DEVICE_TABLE(usb,...).  This must be set
 *or your driver's probe function will never get called.
*/
MODULE_DEVICE_TABLE (usb, usb_kbd_id_table);

/*Usb keyborad :  identifies USB interface driver to usbcore*/
static struct usb_driver usb_kbd_driver = {
	.name       =  "usbkbd",
	.probe      =  usb_kbd_probe,
	.disconnect =  usb_kbd_disconnect,
	.id_table   =  usb_kbd_id_table,
};

/*Device registration*/
module_usb_driver(usb_kbd_driver);
/*Driver INFO*/
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Chandan Jha <beingchandanjha@gamil.com>");
MODULE_DESCRIPTION("USB HID Boot Protocol keyboard driver");
