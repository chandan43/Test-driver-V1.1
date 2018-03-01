obj-m:=mmap_skel.o

KDIR=/lib/modules/$(shell uname -r)/build

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules
	$(CC) mmap_test.c -o test
clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	rm -rf test
