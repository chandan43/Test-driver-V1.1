#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/mutex.h>
#include "rtc.h"
#define KOBJ_NAME "myrtc"
struct mutex lock;

static unsigned char get_rtc(unsigned char addr){

	int ret;
	unsigned char c;
	ret=mutex_lock_killable(&lock);
	if( ret<0 ){
		 pr_err("interrupted while waiting for mutex\n");
		 return -1;
	}
	outb(addr,ADDRESS_REG);
	c=inb(DATA_REG);
	mutex_unlock(&lock);
	return c;
}
static int set_rtc(unsigned char data, unsigned char addr){
	int ret;
	ret=mutex_lock_killable(&lock);
	if( ret<0 ){
		 pr_err("interrupted while waiting for mutex\n");
		 return -EINTR;
	}
	outb(addr,ADDRESS_REG);
	outb(data,DATA_REG);
	mutex_unlock(&lock);
	return 0;
}
static ssize_t time_show(struct kobject *kobj, struct kobj_attribute *attr, char *buff){
	int cnt;
	struct rtc_time time = { 0 };
	pr_info("name : %s\n",attr->attr.name);
	pr_info("%s: Invoked\n", __func__);
	time.sec  =   get_rtc(SECOND);
	time.min  =   get_rtc(MINUTE);
	time.hour =   get_rtc(HOUR);
	cnt=sprintf(buff,"time: %x:%02x:%02x\n",time.hour,time.min,time.sec);
	return cnt;
}

static ssize_t time_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buff, size_t count){
	struct rtc_time time = { 0 };
	pr_info("%s: Invoked\n", __func__);
	sscanf(buff,"time: %x:%x:%x",&time.hour, &time.min, &time.sec);
	set_rtc(time.sec, SECOND);
	set_rtc(time.min, MINUTE);
	set_rtc(time.hour, HOUR);
	return count;
}
static ssize_t date_show(struct kobject *kobj, struct kobj_attribute *attr, char *buff){
	int ret;
	struct rtc_time time = { 0 };
	pr_info("%s: Invoked\n", __func__);
	time.day  = get_rtc(DAY);
	time.mon  = get_rtc(MONTH);
	time.year = get_rtc(YEAR);
	ret=sprintf(buff,"date: %x/%02x/20%02x\n",time.day,time.mon,time.year);
	return ret;
}

static ssize_t date_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buff, size_t count){
	struct rtc_time time = { 0 };
	pr_info("%s: Invoked\n", __func__);
	sscanf(buff,"date: %x:%02x:%02x",&time.day, &time.mon, &time.year);
	set_rtc(time.day, DAY);
	set_rtc(time.mon, MONTH);
	set_rtc(time.year, YEAR);
	return count;
}

static struct kobj_attribute time_attr = __ATTR(time,0660, time_show, time_store);
static struct kobj_attribute date_attr = __ATTR(date,0660, date_show, date_store);

static struct attribute *rtc_attrs[]={
	&time_attr.attr,
	&date_attr.attr,
	NULL,
};

static struct attribute_group attr_grp={
	.attrs=rtc_attrs,
};
static struct kobject *my_rtc;
static int __init rtcsysfs_init(void){
	int ret;
	pr_info("%s: Initialization of RTC using sysfs interface\n",__func__);
	mutex_init(&lock);
	my_rtc=kobject_create_and_add(KOBJ_NAME,NULL); /* If the kobject was not able to be created, NULL will be returned.2nd Param: kernel_kobj->parent kernel_kobj points to /sys/kernel*/
	if(!my_rtc){
		pr_err("%s: Kobject is not able to create and add\n",__func__);
		return -ENOMEM;
	}
	ret=sysfs_create_group(my_rtc,&attr_grp);
	if(ret)
		kobject_put(my_rtc);
	return 0;
}

static void __exit rtcsysfs_exit(void){
	kobject_put(my_rtc);
	pr_info("%s: RTC is exited Successfully\n",__func__);
}

module_init(rtcsysfs_init);
module_exit(rtcsysfs_exit);

MODULE_LICENSE("GPL");
MODULE_VERSION(".1");
MODULE_AUTHOR("beingchandanjha@gmail.com");
MODULE_DESCRIPTION("RTC : Real time clock using sysfs interface");
