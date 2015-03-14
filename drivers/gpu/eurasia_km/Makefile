obj-$(CONFIG_GPU_EURASIA_SGX544) += pvrsrvkm.o dc_linuxfb.o bc_example.o

EXTRA_CFLAGS += \
	-D__linux__ \
	-include $(src)/platform-r8a7791/config_kernel.h \
	-I$(src)/include4 \
	-I$(src)/services4/include \
	-I$(src)/services4/system/renesas \
	-I$(src)/services4/system/include \
	-I$(src)/services4/srvkm/bridged \
	-I$(src)/services4/srvkm/bridged/sgx \
	-I$(src)/services4/srvkm/common \
	-I$(src)/services4/srvkm/devices/sgx \
	-I$(src)/services4/srvkm/env/linux \
	-I$(src)/services4/srvkm/include \
	-I$(src)/services4/srvkm/hwdefs \
	-I$(src)/services4/3rdparty/dc_linuxfb

dc_linuxfb-objs += \
	services4/3rdparty/dc_linuxfb/dc_linuxfb_displayclass.o \
	services4/3rdparty/dc_linuxfb/dc_linuxfb_linux.o

bc_example-objs += \
	services4/3rdparty/bufferclass_example/bufferclass_example.o \
	services4/3rdparty/bufferclass_example/bufferclass_example_linux.o \
	services4/3rdparty/bufferclass_example/bufferclass_example_private.o

pvrsrvkm-objs += \
	services4/srvkm/env/linux/osfunc.o \
	services4/srvkm/env/linux/mutils.o \
	services4/srvkm/env/linux/mmap.o \
	services4/srvkm/env/linux/module.o \
	services4/srvkm/env/linux/pdump.o \
	services4/srvkm/env/linux/proc.o \
	services4/srvkm/env/linux/pvr_bridge_k.o \
	services4/srvkm/env/linux/pvr_debug.o \
	services4/srvkm/env/linux/mm.o \
	services4/srvkm/env/linux/mutex.o \
	services4/srvkm/env/linux/event.o \
	services4/srvkm/env/linux/osperproc.o \
	services4/srvkm/common/buffer_manager.o \
	services4/srvkm/common/devicemem.o \
	services4/srvkm/common/handle.o \
	services4/srvkm/common/hash.o \
	services4/srvkm/common/lists.o \
	services4/srvkm/common/mem.o \
	services4/srvkm/common/mem_debug.o \
	services4/srvkm/common/metrics.o \
	services4/srvkm/common/osfunc_common.o \
	services4/srvkm/common/pdump_common.o \
	services4/srvkm/common/perproc.o \
	services4/srvkm/common/power.o \
	services4/srvkm/common/pvrsrv.o \
	services4/srvkm/common/ra.o \
	services4/srvkm/common/refcount.o \
	services4/srvkm/common/resman.o \
	services4/srvkm/common/deviceclass.o \
	services4/srvkm/common/queue.o \
	services4/srvkm/bridged/bridged_support.o \
	services4/srvkm/bridged/bridged_pvr_bridge.o \
	services4/srvkm/bridged/sgx/bridged_sgx_bridge.o \
	services4/system/renesas/sysconfig.o \
	services4/system/renesas/sysutils.o \
	services4/srvkm/devices/sgx/sgxinit.o \
	services4/srvkm/devices/sgx/sgxpower.o \
	services4/srvkm/devices/sgx/sgxreset.o \
	services4/srvkm/devices/sgx/sgxutils.o \
	services4/srvkm/devices/sgx/sgxkick.o \
	services4/srvkm/devices/sgx/sgxtransfer.o \
	services4/srvkm/devices/sgx/mmu.o \
	services4/srvkm/devices/sgx/pb.o
