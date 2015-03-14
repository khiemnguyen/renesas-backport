CFILES = mmngr_drv.c

obj-m += mmngr.o
mmngr-objs := $(CFILES:.c=.o)

EXTRA_CFLAGS += -D$(MMNGR_CONFIG)
EXTRA_CFLAGS += -D$(MMNGR_SSP_CONFIG)
EXTRA_CFLAGS += -I$(KERNELSRC)/include

all:
	$(CP) ../include/mmngr_public.h $(KERNELSRC)/include
	$(CP) ../include/mmngr_private.h $(KERNELSRC)/include
	make -C $(KERNELDIR) M=$(PWD) modules
clean:
	make -C $(KERNELDIR) M=$(PWD) clean

