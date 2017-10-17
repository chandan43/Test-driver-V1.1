#obj-m:=mycharS.o
#obj-m+=mycharD.o
#obj-m+=mycharPer.o
#obj-m+=mychar_udev.o
#obj-m+=mychar_reg.o
#obj-m+=mychar_misc.o
obj-m+=rtc_chardrv.o

KDIR=/lib/modules/$(shell uname -r)/build

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules
	$(CC) test.c -o test
	$(CC) rtc_read.c -o rtcr
	$(CC) rtc_write.c -o rtcw
clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	rm -rf test rtcr rtcw
