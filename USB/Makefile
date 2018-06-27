DRIVER_KMOD = usb-storage

obj-m := $(DRIVER_KMOD).o

$(DRIVER_KMOD)-y := scsiglue.o protocol.o transport.o usb_1.o 
$(DRIVER_KMOD)-y += initializers.o sierra_ms.o option_ms.o 
$(DRIVER_KMOD)-y += usual-tables.o

KDIR=/lib/modules/$(shell uname -r)/build

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules 
clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean 	 
