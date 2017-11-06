#include <linux/module.h>
#include <linux/init.h>
#include <linux/pci.h>

#define DRV_NAME	"realtek8139"
#define DRV_VERSION	".1"
#define RTL8139_DRIVER_NAME   DRV_NAME " Fast Ethernet driver " DRV_VERSION

typedef enum {
	RTL8139 = 0,
	RTL8129,
}board_t;

/*# (vendorID, deviceID, subvendor, subdevice, class, class_mask driver_data)
 *  vendorID and deviceID : This 16-bit register identifies a hardware manufacturer and This is another 16-bit register, selected by the manufacturer; no official registration is require    d for the device ID. 
 *  subvendor and subdevice:These specify the PCI subsystem vendor and subsystem device IDs of a device. If a driver can handle any type of subsystem ID, the value PCI_ANY_ID should be u    sed for these fields.
 *  class AND class_mask :These two values allow the driver to specify that it supports a type of PCI class device. The different classes of PCI devices (a VGA controller is one example)    are described in the    PCI specification. If    a driver can handle any type of subsystem ID, the value PCI_ANY_ID should be used for these fields.
 *  driver_data :This value is not used to match a device but is used to hold information that the PCI driver can use to differentiate between different devices if it wants to.
 *  Ref : http://www.makelinux.net/ldd3/chp-12-sect-1
 */
static const struct pci_device_id rtl8139_pci_tbl[] = {
	{0x10ec, 0x8139, PCI_ANY_ID, PCI_ANY_ID, 0, 0, RTL8139},
	/* some crazy cards report invalid vendor ids like
	 * 0x0001 here.  The other ids are valid and constant,
	 * so we simply don't match on the main vendor id.
	 */
	{PCI_ANY_ID, 0x8139, 0x10ec, 0x8139, 0, 0, RTL8139 },
	{0,}
};
MODULE_DEVICE_TABLE (pci, rtl8139_pci_tbl);
/* New device inserted */
static int  rtl8139_init_one(struct pci_dev *dev, const struct pci_device_id *ent)
{
	pr_info("%s: probe invoked\n",__func__);
	return 0;
}
static void rtl8139_remove_one(struct pci_dev *dev)
{
	pr_info("%s: device removed safly\n",__func__);
}
static struct pci_driver rtl8139_pci_driver = {
	.name           = DRV_NAME,
	.id_table	= rtl8139_pci_tbl,
	.probe		= rtl8139_init_one,
	.remove		= rtl8139_remove_one,
};

/*register a pci driver ::- on Success return 0 otherwise errorno*/
static int __init rtl8139_init_module (void)
{
#ifdef MODULE
	pr_info(RTL8139_DRIVER_NAME "\n");
#endif
	return pci_register_driver (&rtl8139_pci_driver);
}

static void __exit rtl8139_cleanup_module (void)
{
	pci_unregister_driver (&rtl8139_pci_driver);
}

module_init(rtl8139_init_module);
module_exit(rtl8139_cleanup_module);

MODULE_LICENSE("GPL");
MODULE_AUTHOR ("Chandan Jha <beingchandanjha@gmail.com>");
MODULE_DESCRIPTION ("RealTek RTL-8139 Fast Ethernet driver");
MODULE_VERSION(DRV_VERSION);


