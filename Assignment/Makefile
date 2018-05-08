#obj-m:=char_dev.o
obj-m:=chardev_poll.o
#obj-m+=chardev_minor.o
#obj-m+=charioctl.o
#obj-m+=chardrv_ioctl.o
#obj-m+=chardev_sync.o
#obj-m+=chardrv_lock.o
#obj-m+=chardrv_seqlock.o

KDIR=/lib/modules/$(shell uname -r)/build

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules
	$(CC) Test_write.c -o testw
	$(CC) Test_read.c -o testr
	$(CC) Test_write1.c -o testw1
	$(CC) Test_read1.c -o testr1
	$(CC) charioctl_test.c -o charioctl
	$(CC) Testr_ioctl.c -o ioctlr
	$(CC) Testw_ioctl.c -o ioctlw
	$(CC) Test_poll.c -o testpoll
clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	rm -rf testw testr testw1 testr1 charioctl ioctlr ioctlw testpoll 
