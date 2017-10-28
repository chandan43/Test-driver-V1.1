#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/fs.h>
#define BASE_PROC_DIR  "Chandan"
#define PROCFS_NAME "myproc1"

struct proc_dir_entry *entry;
static struct proc_dir_entry *base_proc;
static int value=0;

int show_int(struct seq_file *s, void *v){
	seq_printf(s, "Value: %lld\n", (unsigned long long)value);
	return 0;
}
static int proc_open(struct inode *inodep, struct file *filep){
//	try_module_get(THIS_MODULE);
	pr_info("%s: Proc interface opened\n",__func__);
	return single_open(filep,show_int, NULL);
}
/*static int proc_release(struct inode *inodep, struct file *filep){
//	module_put(THIS_MODULE);
	pr_info("%s: Proc interface Closed\n",__func__);
	return 0;
}*/

/*static ssize_t proc_read(struct file *filep, char __user *buffer, size_t size, loff_t *offset){
	int ret=0;
	//ret=put_user(value,buffer);
	ret=copy_to_user(buffer, &value, 4);
	if(ret){
		pr_err("%s: copy failed\n",__func__);
		return ret;
	}
	return size;
}*/
static ssize_t proc_write(struct file *filep, const char __user *buffer, size_t size, loff_t *offset){
	int ret;
	ret = kstrtou32_from_user(buffer, size, 0, &value);
	if(ret){
		pr_err("%s: copy failed\n",__func__);
		return ret;
	}
	pr_info("Value: %d\n",value);
	return size;
}
static struct file_operations proc_fops={
	.owner   =THIS_MODULE,
	.open    =proc_open,
	.read    =seq_read,
	.write   =proc_write,
//	.release =proc_release,
	.release =single_release, 
};

static int __init proc_basic_init(void){
	base_proc=proc_mkdir(BASE_PROC_DIR, NULL);
	if (!base_proc) {
		pr_err("failed to create proc directory: %s\n", BASE_PROC_DIR);
		return -EFAULT;
	}
	entry=proc_create(PROCFS_NAME,S_IFREG|S_IRUGO,base_proc,&proc_fops);
	if(!entry){
		proc_remove(base_proc);
		pr_err("Failed to create proc entry\n");
		return -EFAULT;
	}
	pr_info("Proc:  proc entry Created successfully\n");
	return 0;
}

static void __exit proc_basic_exit(void){
//	remove_proc_entry(PROCFS_NAME,NULL);
	proc_remove(base_proc);
	pr_info("%s:  proc entry deleted successfully\n",__func__);
}

module_init(proc_basic_init);
module_exit(proc_basic_exit);



MODULE_LICENSE("GPL");
MODULE_AUTHOR("beingchandanjha@gmail.com");
MODULE_DESCRIPTION("Basic proc fs example");
MODULE_VERSION(".1");
