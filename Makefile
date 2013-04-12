obj-m += at91-adc.o

#KERNELDIR	= /lib/modules/$(shell uname -r)/build
#KERNELDIR	= ../linux/foxg20-linux-2.6.38
KERNELDIR	= ../linux/ariag25-linux-2.6.39
CROSS_COMPILE=arm-linux-gnueabi-
PWD		:= $(shell pwd)
ARCH = arm

all default:
	$(MAKE) -C $(KERNELDIR) SUBDIRS=$(PWD) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE)  modules
