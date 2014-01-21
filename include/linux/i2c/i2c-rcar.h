#ifndef __I2C_R_CAR_H__
#define __I2C_R_CAR_H__

#include <linux/platform_device.h>

struct i2c_rcar_platform_data {
	u32 bus_speed;
	u32 icccr;
	u32 icccr2;
	u32 icmpr;
	u32 ichpr;
	u32 iclpr;
};

#endif /* __I2C_R_CAR_H__ */
