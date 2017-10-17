obj-m:=char_dev.o

KDIR=/lib/modules/$(shell uname -r)/build

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules
	$(CC) Test_write.c -o testw
	$(CC) Test_read.c -o testr
clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	rm -rf testw testr
