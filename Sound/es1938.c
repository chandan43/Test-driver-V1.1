#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/gameport.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <sound/core.h>
#include <sound/control.h>
#include <sound/pcm.h>
#include <sound/opl3.h>
#include <sound/mpu401.h>
#include <sound/initval.h>
#include <sound/tlv.h>

/*
  Gameport support is for the standard 15-pin PC gameport. If you
  have a joystick, gamepad, gameport card, a soundcard with a gamepor
  or anything else that uses the gameport, say Y or M here and also to
  at least one of the hardware specific drivers.
*/
#if IS_REACHABLE(CONFIG_GAMEPORT)
#define SUPPORT_JOYSTICK 1
#endif


struct es1938 {
	int irq;

	unsigned long io_port;
	unsigned long sb_port;
	unsigned long vc_port;
	unsigned long mpu_port;
	unsigned long game_port;
	unsigned long ddma_port;

	unsigned char irqmask;
	unsigned char revision;

	struct snd_kcontrol *hw_volume;
	struct snd_kcontrol *hw_switch;
	struct snd_kcontrol *master_volume;
	struct snd_kcontrol *master_switch;

	struct pci_dev *pci;
	struct snd_card *card;
	struct snd_pcm *pcm;
	struct snd_pcm_substream *capture_substream;
	struct snd_pcm_substream *playback1_substream;
	struct snd_pcm_substream *playback2_substream;
	struct snd_rawmidi *rmidi;

	unsigned int dma1_size;
	unsigned int dma2_size;
	unsigned int dma1_start;
	unsigned int dma2_start;
	unsigned int dma1_shift;
	unsigned int dma2_shift;
	unsigned int last_capture_dmaaddr;
	unsigned int active;

	spinlock_t reg_lock;
	spinlock_t mixer_lock;
        struct snd_info_entry *proc_entry;

#ifdef SUPPORT_JOYSTICK
	struct gameport *gameport;
#endif
#ifdef CONFIG_PM_SLEEP
	unsigned char saved_regs[SAVED_REG_SIZE];
#endif
};

/* New device inserted */
static int  snd_es1938_probe(struct pci_dev *pci, 
			     const struct pci_device_id *id)
{
	
}   

/* Device removed (NULL if not a hot-plug capable driver) */
static void snd_es1938_remove(struct pci_dev *pci)
{
	
}

/**
 * PCI_VDEVICE - macro used to describe a specific pci device in short form
 * @vendor: the vendor name
 * @device: the 16 bit PCI Device ID
 *
 * This macro is used to create a struct pci_device_id that matches a
 * specific PCI device.  The subvendor, and subdevice fields will be set
 * to PCI_ANY_ID. The macro allows the next field to follow as the device
 * private data.
 */

static const struct pci_device_id snd_es1938_ids[] = {
	{ PCI_VDEVICE(ESS, 0x1969), 0, },   /* Solo-1 */
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, snd_es1938_ids);

static struct pci_driver es1938_driver = {
	.name = KBUILD_MODNAME,
	.id_table = snd_es1938_ids,
	.probe = snd_es1938_probe,
	.remove = snd_es1938_remove,
	.driver = {
//		.pm = ES1938_PM_OPS,
	},
};

/**
 * module_pci_driver() - Helper macro for registering a PCI driver
 * @__pci_driver: pci_driver struct
 *
 * Helper macro for PCI drivers which do not do anything special in module
 * init/exit. This eliminates a lot of boilerplate. Each module may only
 * use this macro once, and calling it replaces module_init() and module_exit()
 */
module_pci_driver(es1938_driver);
MODULE_AUTHOR("Chandan Jha <beingchandanjha@gmail.com>");
MODULE_DESCRIPTION("ESS Solo-1, : Rewritten for better understanding of code");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("{{ESS,ES1938}," "{ESS,ES1946},"
                "{ESS,ES1969},""{TerraTec,128i PCI}}");
