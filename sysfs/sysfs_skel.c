#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>

#include "rtc.h"

#define KOBJ_NAME "myrtc"

static ssize_t time_show(struct kobject *kobj, struct kobj_attribute *attr, char *buff){
	return 0;
}

static ssize_t time_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buff, size_t count){
	return count;
}
static ssize_t date_show(struct kobject *kobj, struct kobj_attribute *attr, char *buff){
	return 0;
}

static ssize_t date_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buff, size_t count){
	return count;
}

static struct kobj_attribute time_attr=__ATTR(time,0660,time_show,time_store);
static struct kobj_attribute date_attr=__ATTR(date,0660,date_show,date_store);

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
