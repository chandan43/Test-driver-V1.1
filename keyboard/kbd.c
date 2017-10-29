/* This driver is based on the 2.6.3 version of drivers/hid/usbhid/usbkbd.c
 * but has been rewritten to be easier to read and use
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/usb/input.h>
#include <linux/hid.h>

static int usb_kbd_probe(struct usb_interface *iface,const struct usb_device_id *id){
	pr_info("%s: Invoked Probe function\n",__func__);
	return 0;
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
