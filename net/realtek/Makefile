obj-m :=rtl8139.o
#obj-m:=8139too.o
#mouse-y:=usbmouse.o


KDIR=/lib/modules/$(shell uname -r)/build

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules
clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
