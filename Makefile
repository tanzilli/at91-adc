obj-m := at91-adc.o
obj-m := at91-adc-ng.o

KERNELDIR	?= /lib/modules/$(shell uname -r)/build
PWD		:= $(shell pwd)

all default:
	$(MAKE) -C $(KERNELDIR) SUBDIRS=$(PWD) modules

