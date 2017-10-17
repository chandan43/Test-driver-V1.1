#obj-m:=mycharS.o
obj-m+=rtc_ioctl.o

KDIR=/lib/modules/$(shell uname -r)/build

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules
	$(CC) rtc_read.c -o rtcr
	$(CC) rtcioctl_test.c -o rtcioctl
clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	rm -rf rtcr rtcioctl
