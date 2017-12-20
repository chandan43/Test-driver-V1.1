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

module_usb_stor_driver(usb_storage_driver, usb_stor_host_template, DRV_NAME);
