#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/mutex.h>
#include <linux/miscdevice.h>
#include <asm/uaccess.h>
/*/lib/modules/4.10.0-37-generic/build/arch/x86/include/asm*/
#include "rtc.h"
#define DEVICE_NAME "myrtc"
#define COUNT 1

struct mutex m_lock;
//static DEFINE_MUTEX(m_lock);

static int myrtc_open(struct inode *, struct file *);
static int myrtc_release(struct inode *, struct file *);
static ssize_t myrtc_read(struct file *, char __user *, size_t, loff_t *);
static ssize_t myrtc_write(struct file *, const char __user *, size_t, loff_t *);
static unsigned char get_rtc(unsigned char addr);
static void set_rtc(unsigned char data, unsigned char addr);

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

static int __init mychar_init(void){
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

static void __exit mychar_exit(void){
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
	unsigned int ret;
	struct rtc_time time={0};
	if(len != sizeof(struct rtc_time)){
		pr_err("%s: Invalid request\n",__func__);
		 return -EINVAL;
	}
	pr_info("%s: RTC Sent %zu bytes to the user space application\n",__func__,len);
	time.sec = get_rtc(SECOND);
	time.min = get_rtc(MINUTE);
	time.hour= get_rtc(HOUR);
	time.day = get_rtc(DAY);
	time.mon = get_rtc(MONTH);
	time.year= get_rtc(YEAR);
	ret=copy_to_user(buff,&time,sizeof(time));
	if(ret){
		pr_err("%s: Device Read is failed\n",__func__);
		return -EFAULT;
	}
	return len;
}
static ssize_t myrtc_write(struct file *filep, const char __user *buff, size_t len, loff_t *offset){
	unsigned int ret;
	struct rtc_time time={0};
	if(len != sizeof(struct rtc_time)){
		pr_err("%s: Invalid request\n",__func__);
		 return -EINVAL;
	}
	ret=copy_from_user(&time,buff,sizeof(time)); /* Copy data from user space to kernel space.  Returns number of bytes that could not be copied. On success, this will be zero.*/
	if(ret){
		pr_err("%s: RTC write Failed\n",__func__);
		return -EFAULT;
	}
	set_rtc(time.sec,SECOND);
	set_rtc(time.min,MINUTE);
	set_rtc(time.hour, HOUR);
	set_rtc(time.day, DAY);
	set_rtc(time.mon, MONTH);
	set_rtc(time.year, YEAR);
	return (len);
}
static int myrtc_release(struct inode *inodep, struct file *filep){
	mutex_unlock(&m_lock);
	pr_info("%s: RTC node closed successfully\n",__func__);
	return 0;
}
static unsigned char get_rtc(unsigned char addr){
	outb(addr,ADDRESS_REG);
	return inb(DATA_REG);
}
static void set_rtc(unsigned char data, unsigned char addr){
	outb(addr,ADDRESS_REG);
	outb(data,DATA_REG);
}

module_init(mychar_init);
module_exit(mychar_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("beingchandanjha@gmail.com");
MODULE_DESCRIPTION("Sample char driver for cmos realtime clock");
MODULE_VERSION(".1");
