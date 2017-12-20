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

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static bool enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_PNP;	/* Enable this card */

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for ESS Solo-1 soundcard.");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string for ESS Solo-1 soundcard.");
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "Enable ESS Solo-1 soundcard.");

#define SLIO_REG(chip, x) ((chip)->io_port + ESSIO_REG_##x)

#define SLDM_REG(chip, x) ((chip)->ddma_port + ESSDM_REG_##x)

#define SLSB_REG(chip, x) ((chip)->sb_port + ESSSB_REG_##x)


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
/* Does your device have any DMA addressing limitations?  For example, is
   your device only capable of driving the low order 24-bits of address?
  If so, you need to inform the kernel of this fact.

  For correct operation, you must interrogate the kernel in your device
  probe routine to see if the DMA controller on the machine can properly
  support the DMA addressing limitation your device has.  It is good
  style to do this even if your device holds the default setting,
  because this shows that you did think about these issues wrt. your
  device.

   The query is performed via a call to dma_set_mask_and_coherent()::

	int dma_set_mask_and_coherent(struct device *dev, u64 mask);
	
   it returns zero if your card can perform DMA properly on
   the machine given the address mask you provided.
*/
static int snd_es1938_create(struct snd_card *card,
			     struct pci_dev *pci,
			     struct es1938 **rchip)
{
	struct es1938 *chip;
	int err;
	static struct snd_device_ops ops = {
		.dev_free =	snd_es1938_dev_free,
	};

	*rchip = NULL;
	/* enable PCI device */
	if ((err = pci_enable_device(pci)) < 0)
		return err;
	 /* check, if we can restrict PCI DMA transfers to 24 bits */
	if (dma_set_mask(&pci->dev, DMA_BIT_MASK(24)) < 0 ||
	    dma_set_coherent_mask(&pci->dev, DMA_BIT_MASK(24)) < 0) {
		dev_err(card->dev,
			"architecture does not support 24bit PCI busmaster DMA\n");
		pci_disable_device(pci);
                return -ENXIO;
        }
	/*allocate a chip-specific data with zero filled*/
	chip = kzalloc(sizeof(*chip), GFP_KERNEL);
	if (chip == NULL) {
		pci_disable_device(pci);
		return -ENOMEM;
	}	
	/*Initialize the spin lock*/
	spin_lock_init(&chip->reg_lock);
	spin_lock_init(&chip->mixer_lock);
	chip->card = card;
	chip->pci = pci;
	chip->irq = -1; // Dynamic irq alloc
	/*PCI Resource Management*/
	if ((err = pci_request_regions(pci, "ESS Solo-1")) < 0) {
		kfree(chip);
		pci_disable_device(pci);
		return err;
	}	
	chip->io_port = pci_resource_start(pci, 0);
	chip->sb_port = pci_resource_start(pci, 1);
	chip->vc_port = pci_resource_start(pci, 2);
	chip->mpu_port = pci_resource_start(pci, 3);
	chip->game_port = pci_resource_start(pci, 4);
	/*Request irq*/
	if (request_irq(pci->irq, snd_es1938_interrupt, IRQF_SHARED,
			KBUILD_MODNAME, chip)) {
		dev_err(card->dev, "unable to grab IRQ %d\n", pci->irq);
		snd_es1938_free(chip);
		return -EBUSY;
	}
	ip->irq = pci->irq;
	dev_dbg(card->dev,
		"create: io: 0x%lx, sb: 0x%lx, vc: 0x%lx, mpu: 0x%lx, game: 0x%lx\n",
		   chip->io_port, chip->sb_port, chip->vc_port, chip->mpu_port, chip->game_port);
	chip->ddma_port = chip->vc_port + 0x00;		/* fix from Thomas Sailer */
	
	snd_es1938_chip_init(chip); //TODO
	
	/*Attach the components (devices) to the card instance*/
	if ((err = snd_device_new(card, SNDRV_DEV_LOWLEVEL, chip, &ops)) < 0) {
		snd_es1938_free(chip);
		return err;
	}

	*rchip = chip;
	return 0;		
}

/**
 * snd_ctl_notify - Send notification to user-space for a control change
 * @card: the card to send notification
 * @mask: the event mask, SNDRV_CTL_EVENT_*
 * @id: the ctl element id to send notification
 *
 * This function adds an event record with the given id and mask, appends
 * to the list and wakes up the user-space for notification.  This can be
 * called in the atomic context.
 */
/**
 * snd_mpu401_uart_interrupt - generic MPU401-UART interrupt handler
 * @irq: the irq number
 * @dev_id: mpu401 instance
 *
 * Processes the interrupt for MPU401-UART i/o.
 *
 * Return: %IRQ_HANDLED if the interrupt was handled. %IRQ_NONE otherwise.
 */

/* --------------------------------------------------------------------
 * Interrupt handler
 * -------------------------------------------------------------------- */
static irqreturn_t snd_es1938_interrupt(int irq, void *dev_id)
{
	struct es1938 *chip = dev_id;
	unsigned char status, audiostatus;
	int handled = 0;
	/*Reading IRQCONTROL reg*/
	status = inb(SLIO_REG(chip, IRQCONTROL));
#if 0
	dev_dbg(chip->card->dev,
		"Es1938debug - interrupt status: =0x%x\n", status);
#endif
	/* AUDIO 1 */
	if (status & 0x10) {
#if 0
		dev_dbg(chip->card->dev,
		       "Es1938debug - AUDIO channel 1 interrupt\n");
		dev_dbg(chip->card->dev,
		       "Es1938debug - AUDIO channel 1 DMAC DMA count: %u\n",
		       inw(SLDM_REG(chip, DMACOUNT)));
		dev_dbg(chip->card->dev,
		       "Es1938debug - AUDIO channel 1 DMAC DMA base: %u\n",
		       inl(SLDM_REG(chip, DMAADDR)));
		dev_dbg(chip->card->dev,
		       "Es1938debug - AUDIO channel 1 DMAC DMA status: 0x%x\n",
		       inl(SLDM_REG(chip, DMASTATUS)));
#endif
		/* clear irq */
		handled = 1;
		audiostatus = inb(SLSB_REG(chip, STATUS));
		if (chip->active & ADC1)  //TODO
			snd_pcm_period_elapsed(chip->capture_substream);
		else if (chip->active & DAC1)
			snd_pcm_period_elapsed(chip->playback2_substream);
	}
	/* AUDIO 2 */
	if (status & 0x20) {
#if 0
		dev_dbg(chip->card->dev,
		       "Es1938debug - AUDIO channel 2 interrupt\n");
		dev_dbg(chip->card->dev,
		       "Es1938debug - AUDIO channel 2 DMAC DMA count: %u\n",
		       inw(SLIO_REG(chip, AUDIO2DMACOUNT)));
		dev_dbg(chip->card->dev,
		       "Es1938debug - AUDIO channel 2 DMAC DMA base: %u\n",
		       inl(SLIO_REG(chip, AUDIO2DMAADDR)));

#endif
		/* clear irq */
		handled = 1;
		snd_es1938_mixer_bits(chip, ESSSB_IREG_AUDIO2CONTROL2, 0x80, 0); //TODO
		if (chip->active & DAC2)
			snd_pcm_period_elapsed(chip->playback1_substream);
	}
	/* Hardware volume */
	if (status & 0x40) {
		/* Master volume control : Checking Split mode*/
		int split = snd_es1938_mixer_read(chip, 0x64) & 0x80; 
		handled = 1;
		snd_ctl_notify(chip->card, SNDRV_CTL_EVENT_MASK_VALUE, &chip->hw_switch->id);
		snd_ctl_notify(chip->card, SNDRV_CTL_EVENT_MASK_VALUE, &chip->hw_volume->id);
		if (!split) {
			snd_ctl_notify(chip->card, SNDRV_CTL_EVENT_MASK_VALUE,
				       &chip->master_switch->id);
			snd_ctl_notify(chip->card, SNDRV_CTL_EVENT_MASK_VALUE,
				       &chip->master_volume->id);
		}
		/* ack interrupt */
		snd_es1938_mixer_write(chip, 0x66, 0x00);
	}

	/* MPU401 */
	if (status & 0x80) {
		// the following line is evil! It switches off MIDI interrupt handling after the first interrupt received.
		// replacing the last 0 by 0x40 works for ESS-Solo1, but just doing nothing works as well!
		// andreas@flying-snail.de
		// snd_es1938_mixer_bits(chip, ESSSB_IREG_MPU401CONTROL, 0x40, 0); /* ack? */
		if (chip->rmidi) {
			handled = 1;
			snd_mpu401_uart_interrupt(irq, chip->rmidi->private_data);
		}
	}
	return IRQ_RETVAL(handled);
}

/**
 * snd_ctl_new1 - create a control instance from the template
 * @ncontrol: the initialization record
 * @private_data: the private data to set
 *
 * Allocates a new struct snd_kcontrol instance and initialize from the given 
 * template.  When the access field of ncontrol is 0, it's assumed as
 * READWRITE access. When the count field is 0, it's assumes as one.
 *
 * Return: The pointer of the newly generated instance, or %NULL on failure.
 */

/**
 * snd_ctl_add - add the control instance to the card
 * @card: the card instance
 * @kcontrol: the control instance to add
 *
 * Adds the control instance created via snd_ctl_new() or
 * snd_ctl_new1() to the given card. Assigns also an unique
 * numid used for fast search.
 *
 * It frees automatically the control which cannot be added.
 *
 * Return: Zero if successful, or a negative error code on failure.
 *
 */
#define ES1938_DMA_SIZE 64
static int snd_es1938_mixer(struct es1938 *chip)
{
	struct snd_card *card;
	unsigned int idx;
	int err;

	card = chip->card;
	strcpy(card->mixername, "ESS Solo-1");
	for (idx = 0; idx < ARRAY_SIZE(snd_es1938_controls); idx++) {
		struct snd_kcontrol *kctl;
		kctl = snd_ctl_new1(&snd_es1938_controls[idx], chip);
		switch(idx){
			case 0:
				chip->master_volume = kctl;
				kctl->private_free = snd_es1938_hwv_free;
				break;
			case 1:
				chip->master_switch = kctl;
				kctl->private_free = snd_es1938_hwv_free;
				break;
			case 2:
				chip->hw_volume = kctl;
				kctl->private_free = snd_es1938_hwv_free;
				break;
			case 3:
				chip->hw_switch = kctl;
				kctl->private_free = snd_es1938_hwv_free;
				break;
		}
		if ((err = snd_ctl_add(card, kctl)) < 0)
			return err;
	}	
	return 0;		
}
/*
	unsigned long pci_resource_start(struct pci_dev *dev, int bar);
	The function returns the first address (memory address or I/O port number)
	associated with one of the six PCI I/O regions. The region is selected by the inte-ger
	bar
 	(the base address register), ranging from 0–5 (inclusive).
	unsigned long pci_resource_end(struct pci_dev *dev, int bar);
	The function returns the last address that is part of the I/O region number bar
.
	Note that this is the last usable address, not the first address after the region.
	unsigned long pci_resource_flags(struct pci_dev *dev, int bar);
	This function returns the flags associated with this resource.
*/
/**
 * snd_card_free - frees given soundcard structure
 * @card: soundcard structure
 *
 * This function releases the soundcard structure and the all assigned
 * devices automatically.  That is, you don't have to release the devices
 * by yourself.
 *
 * This function waits until the all resources are properly released.
 *
 * Return: Zero. Frees all associated devices and frees the control
 * interface associated to given soundcard.
 */
 /*When the accessing the hardware requires special method instead of 
   the standard I/O access, you can create opl3 instancei */
/**
 * snd_mpu401_uart_new - create an MPU401-UART instance
 * @card: the card instance
 * @device: the device index, zero-based
 * @hardware: the hardware type, MPU401_HW_XXXX
 * @port: the base address of MPU401 port
 * @info_flags: bitflags MPU401_INFO_XXX
 * @irq: the ISA irq number, -1 if not to be allocated
 * @rrawmidi: the pointer to store the new rawmidi instance
 *
 * Creates a new MPU-401 instance.
 *
 * Note that the rawmidi instance is returned on the rrawmidi argument,
 * not the mpu401 instance itself.  To access to the mpu401 instance,
 * cast from rawmidi->private_data (with struct snd_mpu401 magic-cast).
 *
 * Return: Zero if successful, or a negative error code.
 */
/*Step 1 : 1 Check and increment the device index.*/
/*Step 2 : Create a card instance*/
/*Step 3 : Create a main component*/
/*Step 4 : Set the driver ID and name strings.*/
/*Step 5 : Create other components, such as mixer, MIDI, etc.*/
	   /*http://www.alsa-project.org/~tiwai/writing-an-alsa-driver/ch10.html*/
/*Step 6:  Register the card instance.*/
/*Step 7:   Set the PCI driver data and return zero.*/
/* New device inserted */
static int  snd_es1938_probe(struct pci_dev *pci, 
			     const struct pci_device_id *id)
{
	static int dev;
	/* main structure for soundcard */
	struct snd_card *card;
	struct es1938 *chip;
	struct snd_opl3 *opl3;
	int idx, err;
	/*Each time the probe callback is called, check the availability 
 	  of the device. If not available, simply increment the device 
          index and returns */
//Step 1:
	if (dev >= SNDRV_CARDS)
		return -ENODEV;
	if (!enable[dev]) {
		dev++;
		return -ENOENT;
	}

//Step 2:
	/*Create a card instance*/
	err = snd_card_new(&pci->dev, index[dev], id[dev], THIS_MODULE,
			   0, &card);
	if (err < 0)
		return err;
//Step 3:
	/* IORESOURCE_IO : PCI/ISA I/O ports */
	for (idx = 0; idx < 5; idx++) {
		if (pci_resource_start(pci, idx) == 0 ||
		    !(pci_resource_flags(pci, idx) & IORESOURCE_IO)) {
		    	snd_card_free(card);
		    	return -ENODEV;
		}
	}
	if ((err = snd_es1938_create(card, pci, &chip)) < 0) {      //TODO:
		snd_card_free(card);
		return err;
	}
//Step 4:
	/*private data for soundcard */
	card->private_data = chip;
	strcpy(card->driver, "ES1938");
	strcpy(card->shortname, "ESS ES1938 (Solo-1)");
	sprintf(card->longname, "%s rev %i, irq %i",
		card->shortname,
		chip->revision,
		chip->irq);
//Step 5:
	/*Create other components, such as mixer, MIDI, etc.*/
	//TODO :
	if ((err = snd_es1938_new_pcm(chip, 0)) < 0) {
		snd_card_free(card);
		return err;
	}
	//TODO :
	if ((err = snd_es1938_mixer(chip)) < 0) {
		snd_card_free(card);
		return err;
	}

	/*FM registers can be directly accessed through the direct-FM API,*/
	/*To create the OPL3 component : create opl3 instance*/
	if (snd_opl3_create(card,
			    SLSB_REG(chip, FMLOWADDR),
			    SLSB_REG(chip, FMHIGHADDR),
			    OPL3_HW_OPL3, 1, &opl3) < 0) {
		dev_err(card->dev, "OPL3 not detected at 0x%lx\n",
			   SLSB_REG(chip, FMLOWADDR));
	} else {
	        if ((err = snd_opl3_timer_new(opl3, 0, 1)) < 0) {
	                snd_card_free(card);
	                return err;
		}
	        if ((err = snd_opl3_hwdep_new(opl3, 0, 1, NULL)) < 0) {
	                snd_card_free(card);
	                return err;
		}
	}
	/* create an MPU401-UART instance */
	if (snd_mpu401_uart_new(card, 0, MPU401_HW_MPU401,
				chip->mpu_port,
				MPU401_INFO_INTEGRATED | MPU401_INFO_IRQ_HOOK,
				-1, &chip->rmidi) < 0) {
		dev_err(card->dev, "unable to initialize MPU-401\n");
	} else {
		// this line is vital for MIDI interrupt handling on ess-solo1
		// andreas@flying-snail.de
		snd_es1938_mixer_bits(chip, ESSSB_IREG_MPU401CONTROL, 0x40, 0x40);
	}
	snd_es1938_create_gameport(chip);

//STEP 6:
	
	/* Register the card instance.*/
	if ((err = snd_card_register(card)) < 0) {
		snd_card_free(card);
		return err;
	}

//STEP 7: 
	/* Set the PCI driver data  */
	pci_set_drvdata(pci, card);
	dev++;
	return 0;			
}   

/* Device removed (NULL if not a hot-plug capable driver) */
static void snd_es1938_remove(struct pci_dev *pci)
{
	snd_card_free(pci_get_drvdata(pci));
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
