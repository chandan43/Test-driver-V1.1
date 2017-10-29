/* ttyled_blink.c - Blink keyboard leds until the module is unloaded.modified for 4.10.0-37*/

#include <linux/module.h>
#include <linux/configfs.h>
#include <linux/init.h>
#include <linux/tty.h>		/* For fg_console, MAX_NR_CONSOLES */
#include <linux/kd.h>		/* For KDSETLED */
#include <linux/vt.h>
#include <linux/console_struct.h>	/* For vc_cons */
#include <linux/vt_kern.h>

struct timer_list mytimer;
struct tty_driver *ttydriver;
char kbledstatus = 0;

#define BLINK_DELAY   HZ/5
#define ALL_LEDS_ON   0x07
#define RESTORE_LEDS  0xFF

static void mytimer_func(unsigned long ptr)
{
	int *pstatus = (int *)ptr;

	if(*pstatus == ALL_LEDS_ON)
		*pstatus = RESTORE_LEDS;
	else
		*pstatus = ALL_LEDS_ON;

	(ttydriver->ops->ioctl) (vc_cons[fg_console].d->port.tty, KDSETLED,*pstatus);

	mytimer.expires = jiffies + BLINK_DELAY;
	add_timer(&mytimer);
}

static int __init ttyled_blink_init(void)
{
	int i;

	pr_info("TTYLED: Loading\n");
	pr_info("TTYLED: fgconsole is %x\n", fg_console);
	for (i = 0; i < MAX_NR_CONSOLES; i++) {
		if (!vc_cons[i].d)
			break;
		pr_info("POET_ATKM: console[%i/%i] #%i, tty %lx\n", i,MAX_NR_CONSOLES, vc_cons[i].d->vc_num,
		       (unsigned long)vc_cons[i].d->port.tty);
	}
	pr_info("TTYLED: finished scanning consoles\n");

	ttydriver = vc_cons[fg_console].d->port.tty->driver;
	pr_info("TTYLED: tty driver magic %x\n", ttydriver->magic);

	/*
	 * Set up the LED blink timer the first time
	 */
	init_timer(&mytimer);
	mytimer.function = mytimer_func;
	mytimer.data = (unsigned long)&kbledstatus;
	mytimer.expires = jiffies + BLINK_DELAY;
	add_timer(&mytimer);

	return 0;
}

static void __exit ttyled_blink_exit(void)
{
	pr_info("TTYLED: unloading...\n");
	del_timer(&mytimer);
	(ttydriver->ops->ioctl) (vc_cons[fg_console].d->port.tty, KDSETLED,RESTORE_LEDS);
}

module_init(ttyled_blink_init);
module_exit(ttyled_blink_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("beingchandanjha@gmail.com");
MODULE_DESCRIPTION("Example module illustrating the use of Keyboard LEDs for >3.10.0. KERNEL");
