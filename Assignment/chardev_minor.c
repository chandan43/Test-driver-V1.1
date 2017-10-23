/* 
ex2: Allocate 2 kernel buffers of 1024 bytes each(DEVBUFA, DEVBUFB)implement a 
     char driver  that maps DEVBUFA to /dev/bufa  and DEVBUFB to /dev/bufb. 
     Driver should provide support for read/write operations.

Note: Register char driver using dynamic major no's

Task Breakup: 
  
       1. allocate buffer blocks DEVBUFA, and DEVBUFB
       2. register char driver with dynamic major no and 2 minor no's
       3. Implement read and write functions to print minor no associated with 
          request path into dmesg buffer.
       4. Extend driver's read/write routines to transfer data b/w appropriate device 
          buffer an application as per minor no in the request path. 
*/
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/cdev.h>
#include <asm/uaccess.h>

#define DRIVE_NAME "char_dev"
#define CLASS_NAME "VIRTUAL"
#define COUNT 2
#define SIZE 1024

static int minornumber=0;
static int majornumber;
static dev_t mydev;
static struct cdev *mycdev;
struct class *charclass;
struct device *chardevice;
static void *kbuffer;
static void *kbuffer1;
static int numdev=0;
static ssize_t size_of_msg;
static ssize_t chardev_read(struct file *, char __user *, size_t, loff_t *);
static ssize_t chardev_write(struct file *, const char __user *, size_t, loff_t *);
static int chardev_open(struct inode *, struct file *);
static int chardev_release(struct inode *, struct file *);


static struct file_operations fops ={
	.owner   = THIS_MODULE,
	.open    = chardev_open,
	.release = chardev_release,
	.read    = chardev_read,
	.write   = chardev_write,
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
	kbuffer1=kmalloc(SIZE,GFP_KERNEL); //For diffrent minor number
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
	kfree(kbuffer1);
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
static ssize_t chardev_read(struct file *filep, char __user *buffer, size_t size, loff_t *offset){
	int err_count=0;
	size_of_msg=size;
	if(iminor(filep->f_inode)==0)
		err_count=copy_to_user(buffer,kbuffer,size_of_msg);
	else if(iminor(filep->f_inode)==1)
		err_count=copy_to_user(buffer,kbuffer1,size_of_msg);

	if(!err_count){
		pr_info("%s: Sent %zu characters to the user space\n",__func__,size_of_msg);
		return size_of_msg;
	}
	pr_err("%s: Read operation failed\n",__func__);
	return -EFAULT;
}
static ssize_t chardev_write(struct file *filep, const char __user *buffer, size_t size, loff_t *offset){
	int err_count=0;
	size_of_msg=size;
	if(iminor(filep->f_inode)==0)
		err_count=copy_from_user(kbuffer,buffer,size_of_msg);
	else if(iminor(filep->f_inode)==1)
		err_count=copy_from_user(kbuffer1,buffer,size_of_msg);

	if(err_count){
		pr_err("%s: Write operation failed\n",__func__);
		return -EFAULT;
	}
	pr_info("%s: Received %zu characters from the user space\n",__func__,size_of_msg);
	return size_of_msg;
}

module_init(char_dev_init);
module_exit(char_dev_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("beingchandanjha@gmail.com");
MODULE_DESCRIPTION("Char Driver");
MODULE_VERSION(".1");

