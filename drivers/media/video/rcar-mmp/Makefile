obj-$(CONFIG_VIDEO_RCAR_MMP_MMNGR) += mmngr.o
obj-$(CONFIG_VIDEO_RCAR_MMP_VSPM) += vspm.o
obj-$(CONFIG_VIDEO_RCAR_MMP_UVCS) += uvcs_cmn.o
obj-$(CONFIG_VIDEO_RCAR_MMP_S3CTL) += s3ctl.o
obj-$(CONFIG_VIDEO_RCAR_MMP_FDPM) += fdpm.o

EXTRA_CFLAGS += \
	-DCC_HAVE_ASM_GOTO \
	-DMMNGR_KOELSCH \
	-DUVCS_DEBUG=1 \
	-DM2CONFIG \
	-I$(src)/fdpm/fdpm-module/files/fdpm/drv \
	-I$(src)/fdpm/fdpm-module/files/fdpm/include \
	-I$(src)/vspm/vspm-module/files/vspm/drv \
	-I$(src)/vspm/vspm-module/files/vspm/include \
	-I$(src)/mmngr/mmngr-module/files/mmngr/include \
	-I$(src)/uvcs_cmn/include \
	-I$(src)/uvcs_cmn/driver_core \
	-I$(src)/./s3ctl/s3ctl-module/files/s3ctl/include

mmngr-objs += \
	mmngr/mmngr-module/files/mmngr/drv/mmngr_drv.o

vspm-objs += \
	vspm/vspm-module/files/vspm/drv/vspm_main.o \
	vspm/vspm-module/files/vspm/drv/vspm_ioctl.o \
	vspm/vspm-module/files/vspm/drv/vspm_sub.o \
	vspm/vspm-module/files/vspm/drv/frame.o \
	vspm/vspm-module/files/vspm/drv/manager/vspm_control.o \
	vspm/vspm-module/files/vspm/drv/manager/vspm_drv_2ddmac.o \
	vspm/vspm-module/files/vspm/drv/manager/vspm_drv_vsp.o \
	vspm/vspm-module/files/vspm/drv/manager/vspm_exec_manager.o \
	vspm/vspm-module/files/vspm/drv/manager/vspm_job_manager.o \
	vspm/vspm-module/files/vspm/drv/manager/vspm_lib.o \
	vspm/vspm-module/files/vspm/drv/manager/vspm_sort_queue.o \
	vspm/vspm-module/files/vspm/drv/manager/vspm_task.o \
	vspm/vspm-module/files/vspm/drv/tddmac/tddmac_drv.o \
	vspm/vspm-module/files/vspm/drv/tddmac/tddmac_drv_table.o \
	vspm/vspm-module/files/vspm/drv/vsp/vsp_drv.o \
	vspm/vspm-module/files/vspm/drv/vsp/vsp_drv_phy.o \
	vspm/vspm-module/files/vspm/drv/vsp/vsp_drv_par.o

uvcs_cmn-objs += \
	uvcs_cmn/uvcs_lkm/uvcs_lkm.o \
	uvcs_cmn/uvcs_lkm/uvcs_lkm_uf_semaphore.o \
	uvcs_cmn/uvcs_lkm/uvcs_lkm_uf_thread.o \
	uvcs_cmn/uvcs_lkm/uvcs_lkm_uf_other.o \
	uvcs_cmn/uvcs_lkm/uvcs_lkm_uf_io.o \
	uvcs_cmn/uvcs_cmn/uvcs_cmn_api.o \
	uvcs_cmn/uvcs_cmn/uvcs_cmn_dump.o \
	uvcs_cmn/driver_core/mcvx_api.o


s3ctl-objs += \
	s3ctl/s3ctl-module/files/s3ctl/drv/s3ctl_drv.o

fdpm-objs += \
	fdpm/fdpm-module/files/fdpm/drv/fdpm_sub.o \
	fdpm/fdpm-module/files/fdpm/drv/fdpm_ioctl.o \
	fdpm/fdpm-module/files/fdpm/drv/fdpm_ctrl.o \
	fdpm/fdpm-module/files/fdpm/drv/fdpm_task.o \
	fdpm/fdpm-module/files/fdpm/drv/fdpm_main.o \
	fdpm/fdpm-module/files/fdpm/drv/fdp/fdp_drv.o \
	fdpm/fdpm-module/files/fdpm/drv/fdp/fdp_drv_l.o \
	fdpm/fdpm-module/files/fdpm/drv/fdp/fdp_drv_imgset.o \
	fdpm/fdpm-module/files/fdpm/drv/manager/fdpm_seq.o \
	fdpm/fdpm-module/files/fdpm/drv/manager/fdpm_resource.o
