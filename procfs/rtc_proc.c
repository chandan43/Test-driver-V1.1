#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/fs.h>
#include <linux/mutex.h>

#include "rtc.h"


#define BASE_PROC_DIR  "RTC"
#define P_TIME "time"
#define P_DATE "date"

struct mutex lock;
struct proc_dir_entry *timeentry;
struct proc_dir_entry *dateentry;
static struct proc_dir_entry *base_proc;


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

int show_time(struct seq_file *s, void *v){
	struct rtc_time time = { 0 };
	time.sec  =   get_rtc(SECOND);
	time.min  =   get_rtc(MINUTE);
	time.hour =   get_rtc(HOUR);
	seq_printf(s, "time: %x:%02x:%02x\n", time.hour,time.min,time.sec);
	return 0;
}
int show_date(struct seq_file *s, void *v){
	struct rtc_time time = { 0 };
	time.day  = get_rtc(DAY);
	time.mon  = get_rtc(MONTH);
	time.year = get_rtc(YEAR);
	seq_printf(s, "date: %x/%02x/20%02x\n", time.day,time.mon,time.year);
	return 0;
}
static int time_open(struct inode *inodep, struct file *filep){
	pr_info("%s: Proc interface opened\n",__func__);
	return single_open(filep,show_time, NULL);
}
static int date_open(struct inode *inodep, struct file *filep){
	pr_info("%s: Proc interface opened\n",__func__);
	return single_open(filep,show_date, NULL);
}
static ssize_t time_write(struct file *filep, const char __user *buffer, size_t size, loff_t *offset){
	struct rtc_time time = { 0 };
	sscanf(buffer,"time: %x:%x:%x",&time.hour, &time.min, &time.sec);
	set_rtc(time.hour, HOUR);
	set_rtc(time.min, MINUTE);
	set_rtc(time.sec, SECOND);
//	ret = kstrtou32_from_user(buffer, size, 0, &time.sec);
	return size;
}

static ssize_t date_write(struct file *filep, const char __user *buffer, size_t size, loff_t *offset){
	struct rtc_time time = { 0 };
	sscanf(buffer,"date: %x:%02x:%02x",&time.day, &time.mon, &time.year);
	set_rtc(time.day, DAY);
	set_rtc(time.mon, MONTH);
	set_rtc(time.year, YEAR);
	return size;
}
static struct file_operations date_fops={
	.owner   =THIS_MODULE,
	.open    =date_open,
	.read    =seq_read,
	.write   =date_write,
	.release =single_release, 
};
static struct file_operations time_fops={
	.owner   =THIS_MODULE,
	.open    =time_open,
	.read    =seq_read,
	.write   =time_write,
	.release =single_release, 
};

static int __init proc_rtc_init(void){
	base_proc=proc_mkdir(BASE_PROC_DIR, NULL);
	if (!base_proc) {
		pr_err("failed to create proc directory: %s\n", BASE_PROC_DIR);
		return -EFAULT;
	}
	timeentry=proc_create(P_TIME,S_IFREG|S_IRUGO,base_proc,&time_fops);
	if(!timeentry){
		proc_remove(base_proc);
		pr_err("Failed to create proc entry\n");
		return -EFAULT;
	}
	dateentry=proc_create(P_DATE,S_IFREG|S_IRUGO,base_proc,&date_fops);
	if(!dateentry){
		proc_remove(base_proc);
		pr_err("Failed to create proc entry\n");
		return -EFAULT;
	}
	pr_info("Proc:  proc entry Created successfully\n");
	return 0;
}

static void __exit proc_rtc_exit(void){
//	remove_proc_entry(PROCFS_NAME,NULL);
	proc_remove(base_proc);
	pr_info("%s:  proc entry deleted successfully\n",__func__);
}

module_init(proc_rtc_init);
module_exit(proc_rtc_exit);



MODULE_LICENSE("GPL");
MODULE_AUTHOR("beingchandanjha@gmail.com");
MODULE_DESCRIPTION("RTC example using proc interface");
MODULE_VERSION(".1");
