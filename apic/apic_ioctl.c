#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/mutex.h>
#include <linux/miscdevice.h>
#include <asm/uaccess.h>
#include "apic.h"

struct mutex m_lock;
//static DEFINE_MUTEX(m_lock);
void *io,*ioregsel,*iowin;
int ident,maxirq;


static int myapic_open(struct inode *, struct file *);
static int myapic_release(struct inode *, struct file *);
static long myapic_ioctl(struct file *, unsigned int, unsigned long);
static int numberopen=0;
static const struct file_operations fops={
	.owner   	= THIS_MODULE,
	.open    	= myapic_open,
	.release 	= myapic_release,
	.unlocked_ioctl	= myapic_ioctl,
};
static struct miscdevice APICMISC ={
	.minor=MISC_DYNAMIC_MINOR,
	.name=DEVICE_NAME,
	.fops=&fops,
};

/* 
   ioremap - remaps a physical address range into the 
   processor's virtual address space(kernel's linear address), making
   it available to the kernel
   IOAPIC_BASE: Physical address of IOAPIC
   SIZE: size of the resource to map 
*/
static int __init myapic_init(void){
	int ret;
	mutex_init(&m_lock);
	pr_info("%s: APIC NODE initialization\n",__func__);
	io=ioremap(IOAPIC_BASE,PAGE_SIZE);
	/* 
	   As per IOAPIC Datasheet 0x00 is I/O REGISTER SELECT  
	   of size 32 bits
	 */
	ioregsel=(void *)((long)io+0x00); /*Offset of I/O REgister*/
	/* 
	   As per IOAPIC Datasheet 0x10 is I/O WINDOW REGISTER of size
	   32 bits 
	 */
	iowin=(void *)((long)io+0x10); /*Offset of I/O Windows Register*/
	ret=misc_register(&APICMISC); 
	if(ret){
		pr_err("%s: Misc registeration failed with minornumber %i \n",__func__,APICMISC.minor);
		return ret;
	}
	pr_info("%s: Device Got minornumber %i\n",__func__,APICMISC.minor);
	return 0;

} 

static void __exit myapic_exit(void){
	mutex_destroy(&m_lock);
	misc_deregister(&APICMISC);
	pr_info("Exited Successfully\n");
}
static int myapic_open(struct inode *inodep, struct file *filep){
/*
 * NOTE: mutex_trylock() follows the spin_trylock() convention,
 *       not the down_trylock() convention!
 *
 * Returns 1 if the mutex has been acquired successfully, and 0 on contention.
 */
	if(!mutex_trylock(&m_lock)){
		pr_err("%s:  Device in use by another process\n",__func__);
		return -EBUSY;
	}
	numberopen++;
	pr_info("%s: APIC node opened %d times\n",__func__,numberopen);
	return 0;
};
/* Read IOAPIC IDENTIFICATION 
   As per IOAPIC Datasheet IOAPIC IDENTIFICATION REGISTER 
   Address Offset : 0x00 

   IOAPIC IDENTIFICATION REGISTER

   Bits                 Description
   ********************************
   31:28                Reserved        

   27:24                This 4 bit field contains the IOAPIC 
   			identification.      

   23:0                 Reserved        
 */
/* Read IOAPIC VERSION
   As per OAPIC Datasheet IOAPIC VERSION REGISTER 
   Address Offset : 0x01 

   IOAPIC VERSION REGISTER

   Bits                 Description
   ********************************
   31:24                Reserved        

   23:16                This field contains number of interrupt
   			input pins for the IOAPIC minus one.

   15:8                 Reserved        

   7:0                  This 8 bit field identifies the implementation 
   			version.
 */
static long myapic_ioctl(struct file *filep, unsigned int cmd, unsigned long arg){
	
	if(_IOC_TYPE(cmd)!=APIC_MAGIC)
		return -ENOTTY;
	 if(_IOC_DIR(cmd) & _IOC_READ)
	 	if(!access_ok(VERIFY_WRITE,(void *)arg, _IOC_SIZE(cmd)))
			return -EFAULT;
	 if(_IOC_DIR(cmd) & _IOC_WRITE)
	 	if(!access_ok(VERIFY_READ,(void *)arg, _IOC_SIZE(cmd)))
			return -EFAULT;
	
	switch(cmd){
		case APIC_GETID:
			iowrite32(0,ioregsel);
			ident=ioread32(iowin);
			copy_to_user((int *)arg,&ident,sizeof(ident));
			pr_info("Identification: %08X\n",ident);
			break;
		case APIC_GETIRQ:
			iowrite32(1,ioregsel);
			maxirq=ioread32(iowin);
			/* mask rest and access bit 16-23 */
			maxirq= (maxirq >> 16) & 0x00FF;
			maxirq = maxirq + 1;
			copy_to_user((int *)arg,&maxirq,sizeof(maxirq));
			pr_info("IRQ INFO : %d\n",maxirq);
			break;
	}
	return 0;

}
static int myapic_release(struct inode *inodep, struct file *filep){
	mutex_unlock(&m_lock);
	pr_info("%s: APIC node successfully closed\n",__func__);
	return 0;
}

module_init(myapic_init);
module_exit(myapic_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("beingchandanjha@gmail.com");
MODULE_DESCRIPTION("APIC MMIO Access");
MODULE_VERSION(".1");
