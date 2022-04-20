obj-m := usbkbd.o

KERNEL_DIR = /usr/src/linux-headers-$(shell uname -r)

all:
	$(MAKE) -C $(KERNEL_DIR) M=$(shell pwd) modules

clean:
	rm -rf *.o *.ko *.mod.* *.symvers *.order *~