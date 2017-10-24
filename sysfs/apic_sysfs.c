#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>

#include "apic.h"

#define KOBJ_NAME "myapic"

void *io,*ioregsel,*iowin;
int ident,maxirq;
static int val_lo,val_hi,irqno; 
int ioredtlb[] = { 0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
		   0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B,
		   0x1C, 0x1D, 0x1E, 0x1F, 0x20, 0x21,
		   0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
		   0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D,
		   0x2E, 0x2F, 0x30, 0x31, 0x32, 0x33,
		   0x34, 0x35, 0x36, 0x37, 0x38, 0x39,
		   0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F
};


static ssize_t apicid_show(struct kobject *kobj, struct kobj_attribute *attr, char *buff){
	static int cnt;
	iowrite32(0,ioregsel);
	ident=ioread32(iowin);
	pr_info("Identification: %08X\n",ident);
	cnt=sprintf(buff,"ID: %08X\n",ident);
	return cnt;
}
static ssize_t apicirqs_show(struct kobject *kobj, struct kobj_attribute *attr, char *buff){
	static int cnt;
	iowrite32(1,ioregsel);
	maxirq=ioread32(iowin);
	/* mask rest and access bit 16-23 */
	maxirq= (maxirq >> 16) & 0x00FF;
	maxirq = maxirq + 1;
	cnt=sprintf(buff,"IRQS: %d\n",maxirq);
	return cnt;
}

static ssize_t irqstatus_show(struct kobject *kobj, struct kobj_attribute *attr, char *buff){
	int cnt;
	iowrite32(ioredtlb[irqno*2], ioregsel);
	val_lo = ioread32(iowin);
	iowrite32(ioredtlb[irqno*2+1], ioregsel);
	val_hi = ioread32(iowin);
	pr_info("Redirection-Table entries of IRQ %d: %08X%08X\n",(int)irqno,val_hi,val_lo);
	val_lo=((val_lo & 0x1000)>>12);
	if(val_lo==1)
		cnt=sprintf(buff,"IRQ %d : ON\n",irqno);
	else if(val_lo==0)
		cnt=sprintf(buff,"IRQ %d : OFF\n",irqno);
	return cnt;
}

static ssize_t irqstatus_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buff, size_t count){
	sscanf(buff,"%d",&irqno);
	return count; 
}
static ssize_t irqtype_show(struct kobject *kobj, struct kobj_attribute *attr, char *buff){
	int cnt;
	iowrite32(ioredtlb[irqno*2], ioregsel);
	val_lo = ioread32(iowin);
	iowrite32(ioredtlb[irqno*2+1], ioregsel);
	val_hi = ioread32(iowin);
	pr_info("redirection-table entries of irq %d: %08x%08x\n",(int)irqno,val_hi,val_lo);
	val_lo=((val_lo & 0x0700) >>8);
	if(val_lo==0){
		cnt=sprintf(buff,"IRQTYPE %d : Fixed\n",irqno);
	}
	else if(val_lo==1)
		cnt=sprintf(buff,"IRQTYPE %d : Lowest Priority\n",irqno);
	else if(val_lo==2)
		cnt=sprintf(buff,"IRQTYPE %d : SMI\n",irqno);
	else if(val_lo==3)
		cnt=sprintf(buff,"IRQTYPE %d : Reserved\n",irqno);
	else if(val_lo==4)
		cnt=sprintf(buff,"IRQTYPE %d : NMI\n",irqno);
	else if(val_lo==5)
		cnt=sprintf(buff,"IRQTYPE %d : INIT\n",irqno);
	else if(val_lo==6)
		cnt=sprintf(buff,"IRQTYPE %d : Reserved\n",irqno);
	else if(val_lo==7)
		cnt=sprintf(buff,"IRQTYPE %d : ExtINT\n",irqno);
	return cnt;
}

static ssize_t irqtype_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buff, size_t count){
	sscanf(buff,"%d",&irqno);
	return count; 
}

static struct kobj_attribute apic_getid_attr=__ATTR_RO(apicid);
static struct kobj_attribute apic_getirq_attr=__ATTR_RO(apicirqs);
static struct kobj_attribute apic_irqstatus_attr=__ATTR(irqstatus,0660,irqstatus_show,irqstatus_store);
static struct kobj_attribute apic_irqtype_attr=__ATTR(irqtype,0660,irqtype_show,irqtype_store);

static struct attribute *apic_attrs[]={
	&apic_getirq_attr.attr,
	&apic_getid_attr.attr,
	&apic_irqstatus_attr.attr,
	&apic_irqtype_attr.attr,
	NULL,
};

static struct attribute_group attr_grp={
	.attrs=apic_attrs,
};
static struct kobject *my_apic;
static int __init apicsysfs_init(void){
	int ret;
	pr_info("%s: Initialization of APIC using sysfs interface\n",__func__);
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
	my_apic=kobject_create_and_add(KOBJ_NAME,NULL); /* If the kobject was not able to be created, NULL will be returned.2nd Param: kernel_kobj->parent kernel_kobj points to /sys/kernel*/
	if(!my_apic){
		pr_err("%s: Kobject is not able to create and add\n",__func__);
		return -ENOMEM;
	}
	ret=sysfs_create_group(my_apic,&attr_grp);
	if(ret)
		kobject_put(my_apic);
	return 0;
}

static void __exit apicsysfs_exit(void){
	kobject_put(my_apic);
	pr_info("%s: APIC is exited Successfully\n",__func__);
}

module_init(apicsysfs_init);
module_exit(apicsysfs_exit);

MODULE_LICENSE("GPL");
MODULE_VERSION(".1");
MODULE_AUTHOR("beingchandanjha@gmail.com");
MODULE_DESCRIPTION("APIC : APIC using sysfs interface");
