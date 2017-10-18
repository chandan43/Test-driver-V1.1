obj-m:=apic_ioctl.o

KDIR=/lib/modules/$(shell uname -r)/build

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules
	$(CC) apictest.c -o apict
clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	rm -rf apict
