/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _SM750_HWI2C_H_
#define _SM750_HWI2C_H_

int sm750_hw_i2c_init(void);
int sm750_hw_i2c_read_reg(unsigned char addr, unsigned char reg,
			  unsigned char *data);
int sm750_hw_i2c_write_reg(unsigned char addr, unsigned char reg,
			   unsigned char data);
int sm750_hw_i2c_write_block(unsigned char addr, unsigned char reg,
			    const unsigned char *data, unsigned int length);

#endif
