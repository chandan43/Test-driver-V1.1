#obj-m:=blk_drv.o
#obj-m:=ramblk_drv.o
obj-m:=vd.o
vd-y := ramblock_drv.o

KDIR=/lib/modules/$(shell uname -r)/build

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules
clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
