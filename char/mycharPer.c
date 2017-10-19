#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/capability.h>
#include <linux/sched.h>
#include <linux/mutex.h>
#include <asm/uaccess.h>
#include <asm/current.h>
/*/lib/modules/4.10.0-37-generic/build/arch/x86/include/asm*/

#define DEVICE_NAME "mychar"
#define COUNT 1
#define SIZE 256

struct mutex m_lock;
//static DEFINE_MUTEX(m_lock);


static dev_t mydev;
static struct cdev *my_cdev;
static int mychar_open(struct inode *, struct file *);
static int mychar_release(struct inode *, struct file *);
static ssize_t mychar_read(struct file *, char __user *, size_t, loff_t *);
static ssize_t mychar_write(struct file *, const char __user *, size_t, loff_t *);
static int  majornumber,minornumber;
static int numberopen=0;
static ssize_t size_of_msg;
static char buffer[SIZE]="NULL";

static const struct file_operations fops={
	.owner   = THIS_MODULE,
	.open    = mychar_open,
	.release = mychar_release,
	.read    = mychar_read,
	.write   = mychar_write,
};

static int __init mychar_init(void){
	int ret;
	mutex_init(&m_lock);
	pr_info("%s: Char Driver initialization\n",__func__);
	ret=alloc_chrdev_region(&mydev,0,COUNT,DEVICE_NAME); /*Return value is zero on success, a negative error code on failure*/
	if(ret){
		pr_err("%s: Registeration Chrdev region failed\n",__func__);
		return ret;
	}
	majornumber=MAJOR(mydev);
	minornumber=MINOR(mydev);
	pr_info("%s: Device is registered with %d Major Number and %d Minor Number\n",__func__,majornumber,minornumber);
	my_cdev=cdev_alloc(); /*Allocates and returns a cdev structure, or NULL on failure.*/
	if(!my_cdev){
		pr_err("cdev is not allocated\n");
		goto unregister;
	}
	cdev_init(my_cdev,&fops);
	ret=cdev_add(my_cdev,mydev,COUNT);
	if(ret<0){
		pr_err("%s: cdev_add Failed\n",__func__);
		goto unregister;
	}
	return 0;

unregister:
	unregister_chrdev_region(mydev,COUNT);
	return 0;

} 

static void __exit mychar_exit(void){
	mutex_destroy(&m_lock);
	cdev_del(my_cdev);
	unregister_chrdev_region(mydev,COUNT);
	pr_info("Exited Successfully\n");
}
static int mychar_open(struct inode *inodep, struct file *filep){
/*
 * NOTE: mutex_trylock() follows the spin_trylock() convention,
 *       not the down_trylock() convention!
 *
 * Returns 1 if the mutex has been acquired successfully, and 0 on contention.
 */
	struct task_struct *tsk=current; // get address of task_struct using current macro;
	if(!mutex_trylock(&m_lock)){
		pr_err("%s:  Device in use by another process\n",__func__);
		return -EBUSY;
	}
	pr_info("Caller Process Name %s\n", current->comm);
	pr_info("Flag: %08X",tsk->flags);
//	if(!has_capability(task,CAP_SYS_ADMIN)){
	if(!capable(CAP_SYS_ADMIN)){
	//if(!capable_wrt_inode_uidgid(inodep, CAP_SYS_ADMIN)){
		pr_err("%s:  User application of this USER MODE doesn't have to permision to open device file \n",__func__);
		mutex_unlock(&m_lock);
		return -EPERM;
	}
	numberopen++;
	pr_info("%s: Device opened  %d times\n",__func__,numberopen);
	return 0;
};
static ssize_t mychar_read(struct file *filep, char __user *buff, size_t len, loff_t *offset){
	int err_count=0;
	size_of_msg=len;
	err_count=copy_to_user(buff,buffer,size_of_msg);
	if(err_count==0){
		pr_info("%s: Sent %zu characters to the user space\n",__func__,size_of_msg);
		return (size_of_msg=0);
	}
	pr_err("%s: Device Read is failed\n",__func__);
	return -EFAULT;
}
static ssize_t mychar_write(struct file *filep, const char __user *buff, size_t len, loff_t *offset){
	int err_count=0;
	size_of_msg=len;
	memset(buffer,0,size_of_msg);
	err_count=copy_from_user(buffer,buff,size_of_msg); /* Copy data from user space to kernel space.  Returns number of bytes that could not be copied. On success, this will be zero.*/
	if(err_count){
		pr_err("%s: Device write Failed\n",__func__);
		return -EFAULT;
	}
	pr_info("%s: Received %zu characters from the user space\n",__func__,size_of_msg);
	return (size_of_msg=0);
}
static int mychar_release(struct inode *inodep, struct file *filep){
	mutex_unlock(&m_lock);
	pr_info("%s: Device is closed successfully\n",__func__);
	return 0;
}

module_init(mychar_init);
module_exit(mychar_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("beingchandanjha@gmail.com");
MODULE_DESCRIPTION("Char Device Driver,Dynamic Major Number allocation");
MODULE_VERSION(".1");
