CFILES = s3ctl_drv.c

obj-m += s3ctl.o
s3ctl-objs := $(CFILES:.c=.o)

EXTRA_CFLAGS += -I$(KERNELSRC)/include

all:
	$(CP) ../include/s3ctl_private.h $(KERNELSRC)/include
	make -C $(KERNELDIR) M=$(PWD) modules
clean:
	make -C $(KERNELDIR) M=$(PWD) clean

