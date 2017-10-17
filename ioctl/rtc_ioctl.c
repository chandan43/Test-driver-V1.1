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
static unsigned char get_rtc(unsigned char addr);
static void set_rtc(unsigned char data, unsigned char addr);
static long myrtc_ioctl(struct file *, unsigned int, unsigned long);
static int numberopen=0;
static const struct file_operations fops={
	.owner   	= THIS_MODULE,
	.open    	= myrtc_open,
	.release 	= myrtc_release,
	.read   	= myrtc_read,
	.unlocked_ioctl	= myrtc_ioctl,
};
static struct miscdevice Rtcmisc ={
	.minor=MISC_DYNAMIC_MINOR,
	.name=DEVICE_NAME,
	.fops=&fops,
};

static int __init myrtc_init(void){
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

static void __exit myrtc_exit(void){
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
static long myrtc_ioctl(struct file *filep, unsigned int cmd, unsigned long arg){
	unsigned char data=arg;
	if(_IOC_TYPE(cmd)!=MY_MAGIC)
		return -ENOTTY;
	/*
	 if(_IOC_DIR(cmd) & _IOC_READ)
	 	if(!access_ok(VERIFY_WRITE,(void *)arg, _IOC_SIZE(cmd)))
			return -EFAULT;
	 if(_IOC_DIR(cmd) & _IOC_WRITE)
	 	if(!access_ok(VERIFY_READ,(void *)arg, _IOC_SIZE(cmd)))
			return -EFAULT;
	*/
	switch(cmd){
		case SET_SECOND:
			set_rtc(data,SECOND);
			break;
		case SET_MINUTE:
			set_rtc(data,MINUTE);
			break;
		case SET_HOUR:
			set_rtc(data,HOUR);
			break;
		case SET_DAY:
			set_rtc(data,DAY);
			break;
		case SET_MONTH:
			set_rtc(data,MONTH);
			break;
		case SET_YEAR:
			set_rtc(data,YEAR);
			break;
	}
	return 0;

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

module_init(myrtc_init);
module_exit(myrtc_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("beingchandanjha@gmail.com");
MODULE_DESCRIPTION("Sample char driver for cmos realtime clock");
MODULE_VERSION(".1");
