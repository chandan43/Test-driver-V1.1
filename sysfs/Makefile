#obj-m:=rtc_sysfs.o
#obj-m+=sysfs_skel.o
#obj-m+=rtcsysfs_comman.o
#obj-m+=apic_sysfs.o
obj-m+=rtc_sync.o

KDIR=/lib/modules/$(shell uname -r)/build

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules
clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
