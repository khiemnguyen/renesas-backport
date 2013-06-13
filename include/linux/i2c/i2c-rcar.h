#ifndef __I2C_R_CAR_H__
#define __I2C_R_CAR_H__

#include <linux/platform_device.h>

struct i2c_rcar_platform_data {
	u32 bus_speed;
	u32 icccr_cdf_width;
#define I2C_RCAR_ICCCR_IS_2BIT	0
#define I2C_RCAR_ICCCR_IS_3BIT	1
};

#endif /* __I2C_R_CAR_H__ */
