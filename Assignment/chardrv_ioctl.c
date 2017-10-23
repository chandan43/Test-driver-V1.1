/*
ex4: write a char driver that implements open,release, and ioctl functions.
      Implement ioctl routine to support READ and WRITE commands, that read or 
      write data from/to kernel buffer of size 1024 bytes.

      write an application that uses ioctl api, and execute read, write operations on
      char driver.

Task Breakup :
	
	1. implement a header file and declare ioctl request codes for read/write.
	2. register char driver with ioctl support.
	3. implement ioctl function in driver with READ, WRITE commands.
	4. write application to test ioctl commands.
* */ 
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/cdev.h>
#include <asm/uaccess.h>
#include "charioctl_drv.h"

#define CLASS_NAME "VIRTUALDEV"
#define COUNT 1
#define SIZE 1024

static int minornumber=0;
static int majornumber;
static dev_t mydev;
static struct cdev *mycdev;
struct class *charclass;
struct device *chardevice;
static void *kbuffer;
static int numdev=0;
static ssize_t size_of_msg;
/*static ssize_t chardev_read(struct file *, char __user *, size_t, loff_t *);*/
static ssize_t chardev_write(struct file *, const char __user *, size_t, loff_t *);
static int chardev_open(struct inode *, struct file *);
static int chardev_release(struct inode *, struct file *);
static long chardev_ioctl(struct file *, unsigned int, unsigned long);


static struct file_operations fops ={
	.owner   	= THIS_MODULE,
	.open    	= chardev_open,
	.release 	= chardev_release,
	.unlocked_ioctl	= chardev_ioctl,
};


static int __init char_dev_init(void){
	int ret;
	pr_info("%s: Char driver Initialization\n",__func__);
	ret=alloc_chrdev_region(&mydev,minornumber,COUNT,DRIVE_NAME); /*On Success return 0*/
	if(ret){
		pr_err("%s: allocation of chrdev region Failed\n",__func__);
		return ret;
	}
	majornumber=MAJOR(mydev);
	pr_info("%s: Device is registered with %d major number\n",__func__,majornumber);
	mycdev=cdev_alloc();
	if(!mycdev){
		pr_err("%s: cdev allocation failed\n",__func__);
		goto unregister;
	}
	cdev_init(mycdev,&fops);
	ret=cdev_add(mycdev,mydev,COUNT);
	if(ret){
		pr_err("%s: Cdev is not added successfully\n",__func__);
		goto unregister;
	}
	charclass=class_create(THIS_MODULE,CLASS_NAME);
	if(IS_ERR(charclass)){
		pr_err("%s: Class creation failed\n",__func__);
		goto cdev_del;
	}
	chardevice=device_create(charclass,NULL,MKDEV(majornumber,minornumber),NULL,DRIVE_NAME);
	if(IS_ERR(chardevice)){
		pr_err("%s: Device creation failed\n",__func__);
		goto class_destroy;
	}
	kbuffer=kmalloc(SIZE,GFP_KERNEL);
	if(!kbuffer){
		pr_err("Kbuffer allocation failed\n");
	}
	return 0;
class_destroy:
	class_destroy(charclass);
cdev_del:
	cdev_del(mycdev);
unregister:
	unregister_chrdev_region(mydev,COUNT);
	return ret;
}

static void __exit char_dev_exit(void){
	kfree(kbuffer);
	device_destroy(charclass,MKDEV(majornumber,minornumber));
	class_destroy(charclass);
	cdev_del(mycdev);
	unregister_chrdev_region(mydev,COUNT);
	pr_info("%s: Char driver Exited successfully\n",__func__);
}

static int chardev_open(struct inode *inodep, struct file *filep){
	numdev++;
	pr_info("%s: Device open %d times\n",__func__,numdev);
	return 0;
}
static int chardev_release(struct inode *inodep, struct file *filep){
	pr_info("%s: Device closed successfully\n",__func__);
	return 0;
}
/*static ssize_t chardev_read(struct file *filep, char __user *buffer, size_t size, loff_t *offset){
	int err_count=0;
	err_count=copy_to_user(buffer,kbuffer,size_of_msg);
	if(!err_count){
		pr_info("%s: Sent %zu characters to the user space\n",__func__,size_of_msg);
		return size_of_msg;
	}
	pr_err("%s: Read operation failed\n",__func__);
	return -EFAULT;
}*/
static ssize_t chardev_write(struct file *filep, const char __user *buffer, size_t size, loff_t *offset){
	int err_count=0;
	size_of_msg=size;
	err_count=copy_from_user(kbuffer,buffer,size_of_msg);
	if(err_count){
		pr_err("%s: Write operation failed\n",__func__);
		return -EFAULT;
	}
	pr_info("%s: Received %zu characters from the user space\n",__func__,size_of_msg);
	return size_of_msg;
}
static long chardev_ioctl(struct file *filep, unsigned int cmd, unsigned long arg){
	int ret;
	if(_IOC_TYPE(cmd) != CHAR_MAGIC)
		return -ENOTTY;
	if(_IOC_DIR(cmd)|_IOC_READ)
		if(!access_ok(VERIFY_WRITE,(void *)arg,_IOC_SIZE(cmd)))
			return -EFAULT;
	if(_IOC_DIR(cmd)|_IOC_WRITE)
		if(!access_ok(VERIFY_READ,(void *)arg,_IOC_SIZE(cmd)))
			return -EFAULT;
	switch(cmd){
		case FILE_READ:
		//	chardev_read(filep,(char *)arg,size_of_msg,0)
			ret=copy_to_user((char *)arg,kbuffer,size_of_msg);
			if(ret){
				pr_err("%s: Read operation failed\n",__func__);
				return -EFAULT;
			}
			pr_info("%s: Sent %zu characters to the user space\n",__func__,size_of_msg);
			pr_info("%s: KBUFF data after Read: %s\n",__func__,(char *)kbuffer);
			break;
		case FILE_WRITE:
			chardev_write(filep,(char *)arg,_IOC_SIZE(cmd),0);
			pr_info("%s: KBUFF data after Write: %s\n",__func__,(char *)kbuffer);
			break;
	}
	return 0;
}

module_init(char_dev_init);
module_exit(char_dev_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("beingchandanjha@gmail.com");
MODULE_DESCRIPTION("Char Driver with some IOCTL operation");
MODULE_VERSION(".1");

