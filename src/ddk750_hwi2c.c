// SPDX-License-Identifier: GPL-2.0
/* SM750 hardware I2C master, adapted to use the controller's 8-bit registers. */

#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/io.h>

#include "ddk750_chip.h"
#include "ddk750_hwi2c.h"
#include "ddk750_power.h"
#include "ddk750_reg.h"

#define SM750_I2C_TIMEOUT_US 10000

static inline u8 sm750_i2c_readb(unsigned long reg)
{
	return readb(mmio750 + reg);
}

static inline void sm750_i2c_writeb(unsigned long reg, u8 value)
{
	writeb(value, mmio750 + reg);
}

int sm750_hw_i2c_init(void)
{
	u32 mux = peek32(GPIO_MUX);
	u8 ctrl;

	/* GPIO30 and GPIO31 become the dedicated I2C clock and data pins. */
	poke32(GPIO_MUX, mux | GPIO_MUX_30 | GPIO_MUX_31);
	sm750_enable_i2c(1);

	ctrl = sm750_i2c_readb(I2C_CTRL);
	ctrl &= ~(I2C_CTRL_MODE | I2C_CTRL_CTRL);
	ctrl |= I2C_CTRL_EN; /* Standard mode, 100 kHz. */
	sm750_i2c_writeb(I2C_CTRL, ctrl);
	return 0;
}

static int sm750_hw_i2c_transfer(unsigned char addr, bool read,
				 unsigned char *data, unsigned int count)
{
	u8 status;
	unsigned int i;

	if (!count || count > 16)
		return -EINVAL;

	sm750_i2c_writeb(I2C_RESET, 0);
	sm750_i2c_writeb(I2C_SLAVE_ADDRESS, (addr & 0xfe) | read);
	sm750_i2c_writeb(I2C_BYTE_COUNT, count - 1);

	if (!read)
		for (i = 0; i < count; i++)
			sm750_i2c_writeb(I2C_DATA0 + i, data[i]);

	sm750_i2c_writeb(I2C_CTRL,
			 sm750_i2c_readb(I2C_CTRL) | I2C_CTRL_CTRL);

	for (i = 0; i < SM750_I2C_TIMEOUT_US; i++) {
		status = sm750_i2c_readb(I2C_STATUS);
		if (status & I2C_STATUS_TX)
			break;
		udelay(1);
	}

	if (i == SM750_I2C_TIMEOUT_US)
		return -ETIMEDOUT;
	if (status & I2C_STATUS_ERR)
		return -EIO;
	/* The status bit reports NACK; zero means the target acknowledged. */
	if (status & I2C_STATUS_ACK)
		return -ENXIO;

	if (read)
		for (i = 0; i < count; i++)
			data[i] = sm750_i2c_readb(I2C_DATA0 + i);

	return 0;
}

int sm750_hw_i2c_read_reg(unsigned char addr, unsigned char reg,
			  unsigned char *data)
{
	int ret;

	ret = sm750_hw_i2c_transfer(addr, false, &reg, 1);
	if (ret)
		return ret;
	return sm750_hw_i2c_transfer(addr, true, data, 1);
}

int sm750_hw_i2c_write_reg(unsigned char addr, unsigned char reg,
			   unsigned char data)
{
	unsigned char buf[2] = { reg, data };

	return sm750_hw_i2c_transfer(addr, false, buf, sizeof(buf));
}

int sm750_hw_i2c_write_block(unsigned char addr, unsigned char reg,
			    const unsigned char *data, unsigned int length)
{
	unsigned char buf[16];
	unsigned int i;

	if (!data || !length || length > sizeof(buf) - 1)
		return -EINVAL;
	buf[0] = reg;
	for (i = 0; i < length; i++)
		buf[i + 1] = data[i];

	return sm750_hw_i2c_transfer(addr, false, buf, length + 1);
}
