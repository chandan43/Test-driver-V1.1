#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>  //Required for creating proc entries. 
#include <linux/sched.h> 
#include <linux/workqueue.h> // Required for workqueues


#define SHARED_IRQ 21
static int irq = SHARED_IRQ, my_dev_id = 1, irq_counter = 0;


void workq_fn(struct work_struct *work_arg)
{
	atomic_long_set(&(work_arg->data),10);	
	pr_info("%s: %ld \n",__func__,atomic_long_read(&(work_arg->data)));
}

DECLARE_WORK(workq,(work_func_t)workq_fn);

static irqreturn_t my_interrupt(int irq, void *dev_id)
{
	irq_counter++;
	pr_info("%s In the ISR: counter = %d\n", __func__, irq_counter);
	schedule_work(&workq);
	return IRQ_NONE;/* we return IRQ_NONE because we are just observing */
	/*return IRQ_HANDLED; */
}


int __init workqueue_init(void)
{
	
	if (request_irq(irq, my_interrupt, IRQF_SHARED, "my_interrupt", &my_dev_id))
		return -1;
	/* arg1: irq no
	   arg2: driver's interrupt handler address
	   arg3: priority flag
	   arg4: name of the driver
	   arg5: unique no to identify interrupt handler 
	 */
	return 0;
}
void __exit workqueue_exit(void)
{
	synchronize_irq(irq);
	free_irq(irq, &my_dev_id);
}

module_init(workqueue_init);
module_exit(workqueue_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Chandan Jha : <beingchandanjha@gamil.com>");
MODULE_DESCRIPTION("Linux Work Queue example");
