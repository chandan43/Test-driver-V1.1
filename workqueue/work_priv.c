/*
1. work queues are process context BH that are run by worker kernel threads when system is idle.
   non-cricital deferred work of least priority is generally deferred into work queues

   Usage:

Method 1: Shared work queue

	step 1: Declare work instance
	step 2: initialize work instance with BH routine
	step 3: schedule work object into work queue.

Method 2: private work queue

	step 1: create a private work queue
	step 2: Declare & initialize work instance
	step 3: schedule work into private list
*/


#include <linux/module.h>
#include <linux/fs.h>
#include <linux/workqueue.h>
#include <linux/sched.h>

static struct workqueue_struct *my_workq;
static struct work_struct work;

static void w_fun(struct work_struct *w_arg)
{
 pr_info("current task  name: %s pid :%d\n ", current->comm,(int)current->pid);
}

static int __init my_init(void)
{
	my_workq = create_workqueue("my_workq");
	INIT_WORK(&work, w_fun);
	queue_work(my_workq, &work);
	return 0;

}

static void __exit my_exit(void)
{
	destroy_workqueue(my_workq);
}

module_init(my_init);
module_exit(my_exit);
