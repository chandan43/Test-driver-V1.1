#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/usb.h>
#include <linux/notifier.h>

static int usb_notify(struct notifier_block *self, unsigned long action, void *dev)
{
	pr_info("USB device added\n");
	switch (action) {
		case USB_DEVICE_ADD:
			pr_info("USB device added \n");
			break;
		case USB_DEVICE_REMOVE:
			pr_info("USB device remove\n");
			break;
		case USB_BUS_ADD:
			printk("USB Bus added \n");
			break;
		case USB_BUS_REMOVE:
			printk("USB Bus removed \n");
	}
	return NOTIFY_OK;
}

static struct notifier_block usb_nb = {
	 .notifier_call =        usb_notify,
};

int init_module(void)
{
	pr_info("Init USB hook\n");
	/*
* Hook to the USB core to get notification on any addition or removal of USB devices
	*/
	usb_register_notify(&usb_nb);

	return 0;
}

void cleanup_module(void)
{
	/*
	 * Remove the hook
	*/
	usb_unregister_notify(&usb_nb);

	pr_info("Remove USB hook\n");
}

MODULE_LICENSE("GPL");
