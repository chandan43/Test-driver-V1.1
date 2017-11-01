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

static int usb_mouse_probe(struct usb_interface *intf,const struct usb_device_id *id)
{
	pr_info("%s: Probe invoked\n",__func__);
	return 0;
}
static void usb_mouse_disconnect(struct usb_interface *intf)
{
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
