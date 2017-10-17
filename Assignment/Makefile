#obj-m:=char_dev.o
obj-m:=chardev_minor.o

KDIR=/lib/modules/$(shell uname -r)/build

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules
	$(CC) Test_write.c -o testw
	$(CC) Test_read.c -o testr
	$(CC) Test_write1.c -o testw1
	$(CC) Test_read1.c -o testr1
clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	rm -rf testw testr testw1 testr1
