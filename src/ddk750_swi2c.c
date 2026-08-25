// SPDX-License-Identifier: GPL-2.0
/*
 *         Copyright (c) 2007 by Silicon Motion, Inc. (SMI)
 *
 *  swi2c.c --- SM750/SM718 DDK
 *  This file contains the source code for I2C using software
 *  implementation.
 */

#include "ddk750_chip.h"
#include "ddk750_reg.h"
#include "ddk750_swi2c.h"
#include "ddk750_power.h"
#include <linux/delay.h>

/*
 * I2C Software Master Driver:
 * ===========================
 * Each i2c cycle is split into 4 sections. Each of these section marks
 * a point in time where the SCL or SDA may be changed.
 *
 * 1 Cycle == |  Section I. |  Section 2. |  Section 3. |  Section 4. |
 *            +-------------+-------------+-------------+-------------+
 *            | SCL set LOW |SCL no change| SCL set HIGH|SCL no change|
 *
 *                                          ____________ _____________
 * SCL == XXXX _____________ ____________ /
 *
 * I.e. the SCL may only be changed in section 1. and section 3. while
 * the SDA may only be changed in section 2. and section 4. The table
 * below gives the changes for these 2 lines in the varios sections.
 *
 * Section changes Table:
 * ======================
 * blank = no change, L = set bit LOW, H = set bit HIGH
 *
 *                                | 1.| 2.| 3.| 4.|
 *                 ---------------+---+---+---+---+
 *                 Tx Start   SDA |   | H |   | L |
 *                            SCL | L |   | H |   |
 *                 ---------------+---+---+---+---+
 *                 Tx Stop    SDA |   | L |   | H |
 *                            SCL | L |   | H |   |
 *                 ---------------+---+---+---+---+
 *                 Tx bit H   SDA |   | H |   |   |
 *                            SCL | L |   | H |   |
 *                 ---------------+---+---+---+---+
 *                 Tx bit L   SDA |   | L |   |   |
 *                            SCL | L |   | H |   |
 *                 ---------------+---+---+---+---+
 *
 */

/* GPIO pins used for this I2C. It ranges from 0 to 63. */
static unsigned char sw_i2c_clk_gpio = DEFAULT_I2C_SCL;
static unsigned char sw_i2c_data_gpio = DEFAULT_I2C_SDA;

/*
 *  Below is the variable declaration for the GPIO pin register usage
 *  for the i2c Clock and i2c Data.
 *
 *  Note:
 *      Notice that the GPIO usage for the i2c clock and i2c Data are
 *      separated. This is to make this code flexible enough when
 *      two separate GPIO pins for the clock and data are located
 *      in two different GPIO register set (worst case).
 */

/* i2c Clock GPIO Register usage */
static unsigned long sw_i2c_clk_gpio_mux_reg = GPIO_MUX;
static unsigned long sw_i2c_clk_gpio_data_reg = GPIO_DATA;
static unsigned long sw_i2c_clk_gpio_data_dir_reg = GPIO_DATA_DIRECTION;

/* i2c Data GPIO Register usage */
static unsigned long sw_i2c_data_gpio_mux_reg = GPIO_MUX;
static unsigned long sw_i2c_data_gpio_data_reg = GPIO_DATA;
static unsigned long sw_i2c_data_gpio_data_dir_reg = GPIO_DATA_DIRECTION;

/*
 *  This function puts a delay between command
 */
static void sw_i2c_wait(void)
{
	/* HDMI DDC is limited to 100 kHz; this delay must not optimize away. */
	udelay(5);
}

/*
 *  This function set/reset the SCL GPIO pin
 *
 *  Parameters:
 *      value    - Bit value to set to the SCL or SDA (0 = low, 1 = high)
 *
 *  Notes:
 *      When setting SCL to high, just set the GPIO as input where the pull up
 *      resistor will pull the signal up. Do not use software to pull up the
 *      signal because the i2c will fail when other device try to drive the
 *      signal due to SM50x will drive the signal to always high.
 */
static void sw_i2c_scl(unsigned char value)
{
	unsigned long gpio_data;
	unsigned long gpio_dir;

	gpio_dir = peek32(sw_i2c_clk_gpio_data_dir_reg);
	if (value) {    /* High */
		/*
		 * Set direction as input. This will automatically
		 * pull the signal up.
		 */
		gpio_dir &= ~BIT(sw_i2c_clk_gpio);
		poke32(sw_i2c_clk_gpio_data_dir_reg, gpio_dir);
	} else {        /* Low */
		/* Set the signal down */
		gpio_data = peek32(sw_i2c_clk_gpio_data_reg);
		gpio_data &= ~BIT(sw_i2c_clk_gpio);
		poke32(sw_i2c_clk_gpio_data_reg, gpio_data);

		/* Set direction as output */
		gpio_dir |= BIT(sw_i2c_clk_gpio);
		poke32(sw_i2c_clk_gpio_data_dir_reg, gpio_dir);
	}
}

/*
 *  This function set/reset the SDA GPIO pin
 *
 *  Parameters:
 *      value    - Bit value to set to the SCL or SDA (0 = low, 1 = high)
 *
 *  Notes:
 *      When setting SCL to high, just set the GPIO as input where the pull up
 *      resistor will pull the signal up. Do not use software to pull up the
 *      signal because the i2c will fail when other device try to drive the
 *      signal due to SM50x will drive the signal to always high.
 */
static void sw_i2c_sda(unsigned char value)
{
	unsigned long gpio_data;
	unsigned long gpio_dir;

	gpio_dir = peek32(sw_i2c_data_gpio_data_dir_reg);
	if (value) {    /* High */
		/*
		 * Set direction as input. This will automatically
		 * pull the signal up.
		 */
		gpio_dir &= ~BIT(sw_i2c_data_gpio);
		poke32(sw_i2c_data_gpio_data_dir_reg, gpio_dir);
	} else {        /* Low */
		/* Set the signal down */
		gpio_data = peek32(sw_i2c_data_gpio_data_reg);
		gpio_data &= ~BIT(sw_i2c_data_gpio);
		poke32(sw_i2c_data_gpio_data_reg, gpio_data);

		/* Set direction as output */
		gpio_dir |= BIT(sw_i2c_data_gpio);
		poke32(sw_i2c_data_gpio_data_dir_reg, gpio_dir);
	}
}

/*
 *  This function read the data from the SDA GPIO pin
 *
 *  Return Value:
 *      The SDA data bit sent by the Slave
 */
static unsigned char sw_i2c_read_sda(void)
{
	unsigned long gpio_dir;
	unsigned long gpio_data;
	unsigned long dir_mask = BIT(sw_i2c_data_gpio);

	/* Make sure that the direction is input (High) */
	gpio_dir = peek32(sw_i2c_data_gpio_data_dir_reg);
	if (gpio_dir & dir_mask) {
		gpio_dir &= ~BIT(sw_i2c_data_gpio);
		poke32(sw_i2c_data_gpio_data_dir_reg, gpio_dir);
	}

	/* Now read the SDA line */
	gpio_data = peek32(sw_i2c_data_gpio_data_reg);
	if (gpio_data & BIT(sw_i2c_data_gpio))
		return 1;
	else
		return 0;
}

static unsigned char sw_i2c_read_scl(void)
{
	unsigned long gpio_dir;
	unsigned long gpio_data;
	unsigned long dir_mask = 1UL << sw_i2c_clk_gpio;

	/* Release SCL before sampling it; a slave may be stretching the clock. */
	gpio_dir = peek32(sw_i2c_clk_gpio_data_dir_reg);
	gpio_dir &= ~dir_mask;
	poke32(sw_i2c_clk_gpio_data_dir_reg, gpio_dir);
	gpio_data = peek32(sw_i2c_clk_gpio_data_reg);

	return !!(gpio_data & dir_mask);
}

static bool sw_i2c_raise_scl(void)
{
	unsigned int wait;

	sw_i2c_scl(1);
	for (wait = 0; wait < 100; wait++) {
		if (sw_i2c_read_scl()) {
			sw_i2c_wait();
			return true;
		}
		sw_i2c_wait();
	}

	return false;
}

/* Clock the ninth bit after a received byte: low acknowledges, high NACKs. */
static int sw_i2c_receive_ack(bool ack)
{
	sw_i2c_scl(0);
	sw_i2c_sda(ack ? 0 : 1);
	sw_i2c_wait();
	if (!sw_i2c_raise_scl()) {
		sw_i2c_scl(0);
		sw_i2c_sda(1);
		return -ETIMEDOUT;
	}
	sw_i2c_scl(0);
	sw_i2c_sda(1);
	return 0;
}

/*
 *  This function sends the start command to the slave device
 */
static void sw_i2c_start(void)
{
	/* Start I2C */
	sw_i2c_sda(1);
	sw_i2c_raise_scl();
	sw_i2c_wait();
	sw_i2c_sda(0);
	sw_i2c_wait();
}

/*
 *  This function sends the stop command to the slave device
 */
static void sw_i2c_stop(void)
{
	/* STOP is SDA low-to-high while SCL is high. */
	sw_i2c_scl(0);
	sw_i2c_sda(0);
	sw_i2c_wait();
	sw_i2c_raise_scl();
	sw_i2c_sda(1);
	sw_i2c_wait();
}

/*
 *  This function writes one byte to the slave device
 *
 *  Parameters:
 *      data    - Data to be write to the slave device
 *
 *  Return Value:
 *       0   - Success
 *      -1   - Fail to write byte
 */
static long sw_i2c_write_byte(unsigned char data)
{
	unsigned char value = data;
	int i;

	/* Sending the data bit by bit */
	for (i = 0; i < 8; i++) {
		/* Set SCL to low */
		sw_i2c_scl(0);

		/* Send data bit */
		if ((value & 0x80) != 0)
			sw_i2c_sda(1);
		else
			sw_i2c_sda(0);

		sw_i2c_wait();

		/* Toggle clk line to one */
		if (!sw_i2c_raise_scl())
			return -ETIMEDOUT;

		/* Shift byte to be sent */
		value = value << 1;
	}

	/* Set the SCL Low and SDA High (prepare to get input) */
	sw_i2c_scl(0);
	sw_i2c_sda(1);

	/* Set the SCL High for ack */
	sw_i2c_wait();
	if (!sw_i2c_raise_scl())
		return -ETIMEDOUT;

	/* Keep SCL high while allowing a slow slave/bridge to assert ACK. */
	for (i = 0; i < 0xff; i++) {
		if (!sw_i2c_read_sda())
			break;
		sw_i2c_wait();
	}

	/* Set the SCL Low and SDA High */
	sw_i2c_scl(0);
	sw_i2c_sda(1);

	return i < 0xff ? 0 : -ENXIO;
}

/*
 *  This function reads one byte from the slave device
 *
 *  Parameters:
 *      ack    - Flag to indicate either to send the acknowledge
 *            message to the slave device or not
 *
 *  Return Value:
 *      One byte data read from the Slave device
 */
static int sw_i2c_read_byte(bool ack, unsigned char *result)
{
	int i;
	unsigned char data = 0;
	int ret;

	if (!result)
		return -EINVAL;

	for (i = 7; i >= 0; i--) {
		/* Set the SCL to Low and SDA to High (Input) */
		sw_i2c_scl(0);
		sw_i2c_sda(1);
		sw_i2c_wait();

		/* Set the SCL High */
		if (!sw_i2c_raise_scl()) {
			ret = -ETIMEDOUT;
			goto release;
		}

		/* Read data bits from SDA */
		data |= (sw_i2c_read_sda() << i);
	}

	ret = sw_i2c_receive_ack(ack);
	if (ret)
		goto release;
	*result = data;
	ret = 0;

	release:
	/* Set the SCL Low and SDA High */
	sw_i2c_scl(0);
	sw_i2c_sda(1);

	return ret;
}

/*
 * This function initializes GPIO port for SW I2C communication.
 *
 * Parameters:
 *      clk_gpio      - The GPIO pin to be used as i2c SCL
 *      data_gpio     - The GPIO pin to be used as i2c SDA
 *
 * Return Value:
 *      -1   - Fail to initialize the i2c
 *       0   - Success
 */
static long sm750le_i2c_init(unsigned char clk_gpio, unsigned char data_gpio)
{
	int i;

	/* Initialize the GPIO pin for the i2c Clock Register */
	sw_i2c_clk_gpio_data_reg = GPIO_DATA_SM750LE;
	sw_i2c_clk_gpio_data_dir_reg = GPIO_DATA_DIRECTION_SM750LE;

	/* Initialize the Clock GPIO Offset */
	sw_i2c_clk_gpio = clk_gpio;

	/* Initialize the GPIO pin for the i2c Data Register */
	sw_i2c_data_gpio_data_reg = GPIO_DATA_SM750LE;
	sw_i2c_data_gpio_data_dir_reg = GPIO_DATA_DIRECTION_SM750LE;

	/* Initialize the Data GPIO Offset */
	sw_i2c_data_gpio = data_gpio;

	/* Note that SM750LE don't have GPIO MUX and power is always on */

	/* Clear the i2c lines. */
	for (i = 0; i < 9; i++)
		sw_i2c_stop();

	return 0;
}

/*
 * This function initializes the i2c attributes and bus
 *
 * Parameters:
 *      clk_gpio      - The GPIO pin to be used as i2c SCL
 *      data_gpio     - The GPIO pin to be used as i2c SDA
 *
 * Return Value:
 *      -1   - Fail to initialize the i2c
 *       0   - Success
 */
long sm750_sw_i2c_init(unsigned char clk_gpio, unsigned char data_gpio)
{
	/*
	 * Return 0 if the GPIO pins to be used is out of range. The
	 * range is only from [0..63]
	 */
	if ((clk_gpio > 31) || (data_gpio > 31))
		return -EINVAL;

	if (sm750_get_chip_type() == SM750LE)
		return sm750le_i2c_init(clk_gpio, data_gpio);

	/* Initialize the GPIO pin for the i2c Clock Register */
	sw_i2c_clk_gpio_mux_reg = GPIO_MUX;
	sw_i2c_clk_gpio_data_reg = GPIO_DATA;
	sw_i2c_clk_gpio_data_dir_reg = GPIO_DATA_DIRECTION;

	/* Initialize the Clock GPIO Offset */
	sw_i2c_clk_gpio = clk_gpio;

	/* Initialize the GPIO pin for the i2c Data Register */
	sw_i2c_data_gpio_mux_reg = GPIO_MUX;
	sw_i2c_data_gpio_data_reg = GPIO_DATA;
	sw_i2c_data_gpio_data_dir_reg = GPIO_DATA_DIRECTION;

	/* Initialize the Data GPIO Offset */
	sw_i2c_data_gpio = data_gpio;

	/* Enable the GPIO pins for the i2c Clock and Data (GPIO MUX) */
	poke32(sw_i2c_clk_gpio_mux_reg,
	       peek32(sw_i2c_clk_gpio_mux_reg) & ~BIT(sw_i2c_clk_gpio));
	poke32(sw_i2c_data_gpio_mux_reg,
	       peek32(sw_i2c_data_gpio_mux_reg) & ~BIT(sw_i2c_data_gpio));

	/* Enable GPIO power */
	sm750_enable_gpio(1);

	return sm750_sw_i2c_recover_bus();
}

/*
 *  This function reads the slave device's register
 *
 *  Parameters:
 *      addr   - i2c Slave device address which register
 *                        to be read from
 *      reg    - Slave device's register to be read
 *
 *  Return Value:
 *      Register value
 */
unsigned char sm750_sw_i2c_read_reg(unsigned char addr, unsigned char reg)
{
	unsigned char data = 0xff;

	sm750_sw_i2c_read_reg_checked(addr, reg, &data);
	return data;
}

long sm750_sw_i2c_read_reg_checked(unsigned char addr, unsigned char reg,
				   unsigned char *data)
{
	long ret;

	if (!data)
		return -EINVAL;

	/* Send the Start signal */
	sw_i2c_start();

	/* Select the register, checking that the slave acknowledges each byte. */
	ret = sw_i2c_write_byte(addr);
	if (ret)
		goto stop;
	ret = sw_i2c_write_byte(reg);
	if (ret)
		goto stop;

	/* Get the bus again and get the data from the device read address */
	sw_i2c_start();
	ret = sw_i2c_write_byte(addr + 1);
	if (ret)
		goto stop;
	ret = sw_i2c_read_byte(false, data);

	stop:
	/* Stop swI2C and release the bus */
	sw_i2c_stop();

	return ret;
}

long sm750_sw_i2c_read_block(unsigned char addr, unsigned char offset,
			    unsigned char *data, unsigned int length)
{
	unsigned int i;
	long ret;

	if (!data || !length)
		return -EINVAL;

	sw_i2c_start();
	ret = sw_i2c_write_byte(addr);
	if (ret)
		goto stop;
	ret = sw_i2c_write_byte(offset);
	if (ret)
		goto stop;

	sw_i2c_start();
	ret = sw_i2c_write_byte(addr + 1);
	if (ret)
		goto stop;

	for (i = 0; i < length; i++) {
		ret = sw_i2c_read_byte(i + 1 < length, &data[i]);
		if (ret)
			goto stop;
	}
	ret = 0;

stop:
	sw_i2c_stop();
	return ret;
}

long sm750_sw_i2c_write_block(unsigned char addr, unsigned char offset,
			     const unsigned char *data, unsigned int length)
{
	unsigned int i;
	long ret;

	if (!data || !length)
		return -EINVAL;

	sw_i2c_start();
	ret = sw_i2c_write_byte(addr);
	if (ret)
		goto stop;
	ret = sw_i2c_write_byte(offset);
	if (ret)
		goto stop;
	for (i = 0; i < length; i++) {
		ret = sw_i2c_write_byte(data[i]);
		if (ret)
			goto stop;
	}
	ret = 0;

stop:
	sw_i2c_stop();
	return ret;
}

long sm750_sw_i2c_recover_bus(void)
{
	unsigned int i;

	/*
	 * I2C bus-clear procedure: release SDA, provide at most nine clocks so a
	 * stranded slave can finish its byte, then generate exactly one STOP.
	 * This deliberately replaces the unsafe post-grant repeated-STOP test.
	 */
	sw_i2c_sda(1);
	for (i = 0; i < 9 && !sw_i2c_read_sda(); i++) {
		sw_i2c_scl(0);
		sw_i2c_wait();
		if (!sw_i2c_raise_scl())
			break;
	}
	sw_i2c_stop();

	return (sw_i2c_read_scl() && sw_i2c_read_sda()) ? 0 : -EBUSY;
}

/*
 *  This function writes a value to the slave device's register
 *
 *  Parameters:
 *      addr            - i2c Slave device address which register
 *                        to be written
 *      reg             - Slave device's register to be written
 *      data            - Data to be written to the register
 *
 *  Result:
 *          0   - Success
 *         -1   - Fail
 */
long sm750_sw_i2c_write_reg(unsigned char addr,
			    unsigned char reg,
			    unsigned char data)
{
	long ret;

	/* Send the Start signal */
	sw_i2c_start();

	/* Send the device address and read the data. All should return success
	 * in order for the writing processed to be successful
	 */
	ret = sw_i2c_write_byte(addr);
	if (!ret)
		ret = sw_i2c_write_byte(reg);
	if (!ret)
		ret = sw_i2c_write_byte(data);

	/* Stop i2c and release the bus */
	sw_i2c_stop();

	return ret;
}
