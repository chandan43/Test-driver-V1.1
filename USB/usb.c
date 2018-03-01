#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/kthread.h>
#include <linux/mutex.h>
#include <linux/utsname.h>

#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>

#include "usb.h"
#include "scsiglue.h"
#include "transport.h"
#include "protocol.h"
#include "debug.h"
#include "initializers.h"

#include "sierra_ms.h"
#include "option_ms.h"



/**
 * scsi_host_alloc - register a scsi host adapter instance.
 * @sht:	pointer to scsi host template
 * @privsize:	extra bytes to allocate for driver
 *
 * Note:
 * 	Allocate a new Scsi_Host and perform basic initialization.
 * 	The host is not published to the scsi midlayer until scsi_add_host
 * 	is called.
 *
 * Return value:
 * 	Pointer to a new Scsi_Host
 *
 */

/* First part of general USB mass-storage probing */
int usb_stor_probe1(struct us_data **pus,
		struct usb_interface *intf,
		const struct usb_device_id *id,
		struct us_unusual_dev *unusual_dev)
{
	struct Scsi_Host *host;
	struct us_data *us;
	int result;

	dev_info(&intf->dev, "USB Mass Storage device detected\n");
	/*
	 * Ask the SCSI layer to allocate a host structure, with extra
	 * space at the end for our private us_data structure.
	 */
	host = scsi_host_alloc(&usb_stor_host_template, sizeof(*us));
	if (!host) {
		dev_warn(&intf->dev, "Unable to allocate the scsi host\n");
		return -ENOMEM;
	}

	/*
	 * Allow 16-byte CDBs and thus > 2TB
	 */
	host->max_cmd_len = 16;
	host->sg_tablesize = usb_stor_sg_tablesize(intf);
	*pus = us = host_to_us(host);
	mutex_init(&(us->dev_mutex));
	us_set_lock_class(&us->dev_mutex, intf);
	init_completion(&us->cmnd_ready);
	init_completion(&(us->notify));
	init_waitqueue_head(&us->delay_wait);
	INIT_DELAYED_WORK(&us->scan_dwork, usb_stor_scan_dwork);	
}

//TODO:
/* Handle a USB mass-storage disconnect */
void usb_stor_disconnect(struct usb_interface *intf)
{
	struct us_data *us = usb_get_intfdata(intf);

	quiesce_and_remove_host(us);
	release_everything(us);
}
EXPORT_SYMBOL_GPL(usb_stor_disconnect);

static struct scsi_host_template usb_stor_host_template;

/* The main probe routine for standard devices */
static int storage_probe(struct usb_interface *intf,
					 const struct usb_device_id *id)
{
	struct us_unusual_dev *unusual_dev;
	struct us_data *us;

	int result;
	int size;
	
	/*
	 * If the device isn't standard (is handled by a subdriver
	 * module) then don't accept it.
	 */
	/* Return an error if a device is in the ignore_ids list */
	if (usb_usual_ignore_device(intf))
		return -ENXIO; ///* No such device or address */
 	/*
	 * Call the general probe procedures.
	 *
	 * The unusual_dev_list array is parallel to the usb_storage_usb_ids
	 * table, so we use the index of the id entry to find the
	 * corresponding unusual_devs entry.
	 */

	size = ARRAY_SIZE(us_unusual_dev_list);
	if (id >= usb_storage_usb_ids && id < usb_storage_usb_ids + size) {
		unusual_dev = (id - usb_storage_usb_ids) + us_unusual_dev_list;
	} else {
		unusual_dev = &for_dynamic_ids;

		dev_dbg(&intf->dev, "Use Bulk-Only transport with the Transparent SCSI protocol for dynamic id: 0x%04x 0x%04x\n",
			id->idVendor, id->idProduct);
	}

	result = usb_stor_probe1(&us, intf, id, unusual_dev,
				 &usb_stor_host_template);
	if (result)
		return result;

	/* No special transport or protocol settings in the main module */

	result = usb_stor_probe2(us);
	return result;

}

static struct usb_driver usb_storage_driver = {
	.name =		DRV_NAME,
	.probe =	storage_probe,
	.disconnect =	usb_stor_disconnect,
	.suspend =	usb_stor_suspend,
	.resume =	usb_stor_resume,
	.reset_resume =	usb_stor_reset_resume,
	.pre_reset =	usb_stor_pre_reset,
	.post_reset =	usb_stor_post_reset,
	.id_table =	usb_storage_usb_ids,
	.supports_autosuspend = 1,
	.soft_unbind =	1,
};


/* While accessing a unusual usb storage (ums-alauda, ums-cypress, ...),
 * the module reference count is not incremented.  Because these drivers
 * allocate scsi hosts with usb_stor_host_template defined in usb-storage
 * module.  So these drivers always can be unloaded.
 *
 * This fixes it by preparing scsi host template which is initialized
 * at module_init() for each ums-* driver.  In order to minimize the
 * difference in ums-* drivers, introduce module_usb_stor_driver() helper
 * macro which is same as module_usb_driver() except that it also
 * initializes scsi host template.
 */

//module_usb_stor_driver(usb_storage_driver, usb_stor_host_template, DRV_NAME);
/**
 * module_usb_driver() - Helper macro for registering a USB driver
 * @__usb_driver: usb_driver struct
 *
 * Helper macro for USB drivers which do not do anything special in module
 * init/exit. This eliminates a lot of boilerplate. Each module may only
 * use this macro once, and calling it replaces module_init() and module_exit()
 */
module_usb_driver(usb_storage_driver);
