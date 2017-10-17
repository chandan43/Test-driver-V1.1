#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/mutex.h>
#include <linux/miscdevice.h>
#include <asm/uaccess.h>
/*/lib/modules/4.10.0-37-generic/build/arch/x86/include/asm*/
#include "clock.h"
#define DEVICE_NAME "myclock"
#define FILE_SIZE 6

struct mutex m_lock;
//static DEFINE_MUTEX(m_lock);

unsigned char cmd_arr[]={
	DAY_CMD,
	MONTH_CMD,
	YEAR_CMD,
	HOURS_CMD,
	MINUTES_CMD,
	SECOUND_CMD
};
static int myrtc_open(struct inode *, struct file *);
static int myrtc_release(struct inode *, struct file *);
static ssize_t myrtc_read(struct file *, char __user *, size_t, loff_t *);
static ssize_t myrtc_write(struct file *, const char __user *, size_t, loff_t *);

static int numberopen=0;
static const struct file_operations fops={
	.owner   = THIS_MODULE,
	.open    = myrtc_open,
	.release = myrtc_release,
	.read    = myrtc_read,
	.write   = myrtc_write,
};
static struct miscdevice Rtcmisc ={
	.minor=MISC_DYNAMIC_MINOR,
	.name=DEVICE_NAME,
	.fops=&fops,
};

static int __init myrtc_clock_init(void){
	int ret;
	mutex_init(&m_lock);
	pr_info("%s: Char Driver initialization\n",__func__);
	ret=misc_register(&Rtcmisc); 
	if(ret){
		pr_err("%s: Misc registeration failed with minornumber %i \n",__func__,Rtcmisc.minor);
		return ret;
	}
	pr_info("%s: Device Got minornumber %i\n",__func__,Rtcmisc.minor);
	return 0;

} 

static void __exit myrtc_clock_exit(void){
	mutex_destroy(&m_lock);
	misc_deregister(&Rtcmisc);
	pr_info("Exited Successfully\n");
}
static int myrtc_open(struct inode *inodep, struct file *filep){
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
	pr_info("%s: RTC node opened  %d times\n",__func__,numberopen);
	return 0;
};
static ssize_t myrtc_read(struct file *filep, char __user *buff, size_t len, loff_t *offset){
	unsigned char cmd=0,data=0;
	data=0;
	if(len>1)
		return -EIO; //IO error
	if(*offset >= FILE_SIZE)
		return -EIO; //IO error
	cmd = cmd_arr[*offset];
	READ_FROM_CLOCK(cmd,data);
	if (copy_to_user(buff,&data,1))
		return -EFAULT;
	pr_info("data = %d cmd = %d \n",data,cmd);
	(*offset)++;
	return 1;
}
static ssize_t myrtc_write(struct file *filep, const char __user *buff, size_t len, loff_t *offset){
	unsigned char cmd,data;
	if(len>1)
		return -EIO; //IO error
	if(*offset >= FILE_SIZE)
		return -EIO; //IO error
	cmd = cmd_arr[*offset];
	if(copy_from_user(&data,buff,1))
		return -EFAULT;
	pr_info("cmd = %d  data = %d\n",cmd,data);
	WRITE_TO_CLOCK(cmd,data);
	(*offset)++;
	return 1;
}
static int myrtc_release(struct inode *inodep, struct file *filep){
	mutex_unlock(&m_lock);
	pr_info("%s: RTC node closed successfully\n",__func__);
	return 0;
}

module_init(myrtc_clock_init);
module_exit(myrtc_clock_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("beingchandanjha@gmail.com");
MODULE_DESCRIPTION("Sample char driver for cmos realtime clock-Byte by byte reading");
MODULE_VERSION(".1");
