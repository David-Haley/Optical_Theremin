/*
 * Raspberry Pi Pico platform layer for the ST VL53L4CD ULD driver, using
 * the Pico SDK hardware_i2c driver. The bus (I2C controller) is in the high
 * byte of Dev_t, the 8-bit I2C address in the low byte.
 * Author : David Haley
 */

#include "pico/time.h"

#include "i2c_bus.h"
#include "platform.h"

#define STATUS_OK   0
#define STATUS_FAIL 255

#define MAX_DATA_BYTES 4

static uint8_t bus_of(Dev_t dev)
{
	return (uint8_t)(dev >> 8);
}

static uint8_t address_of(Dev_t dev)
{
	return (uint8_t)((dev & 0xFF) >> 1);
}

/* Writes the big-endian index followed by count bytes of data. */
static uint8_t write_register(Dev_t dev, uint16_t index, const uint8_t *data, int count)
{
	i2c_inst_t *instance = I2C_Bus_Instance(bus_of(dev));
	uint8_t buffer[2 + MAX_DATA_BYTES];

	if (instance == NULL || count > MAX_DATA_BYTES)
		return STATUS_FAIL;

	buffer[0] = (uint8_t)(index >> 8);
	buffer[1] = (uint8_t)(index & 0xFF);
	memcpy(&buffer[2], data, (size_t)count);

	int written = i2c_write_timeout_us(instance, address_of(dev), buffer, (size_t)count + 2, false,
					   I2C_Bus_Timeout_US);

	return written == count + 2 ? STATUS_OK : STATUS_FAIL;
}

/*
 * Writes the big-endian index, then reads count bytes. A STOP separates the
 * two, as in ST's reference platform (CubeIDE_Example).
 */
static uint8_t read_register(Dev_t dev, uint16_t index, uint8_t *data, int count)
{
	i2c_inst_t *instance = I2C_Bus_Instance(bus_of(dev));
	uint8_t buffer[2];

	if (instance == NULL)
		return STATUS_FAIL;

	buffer[0] = (uint8_t)(index >> 8);
	buffer[1] = (uint8_t)(index & 0xFF);

	int written = i2c_write_timeout_us(instance, address_of(dev), buffer, 2, false, I2C_Bus_Timeout_US);
	if (written != 2)
		return STATUS_FAIL;

	int read = i2c_read_timeout_us(instance, address_of(dev), data, (size_t)count, false,
				       I2C_Bus_Timeout_US);

	return read == count ? STATUS_OK : STATUS_FAIL;
}

uint8_t VL53L4CD_RdDWord(Dev_t dev, uint16_t RegisterAdress, uint32_t *value)
{
	uint8_t buffer[4];
	uint8_t status = read_register(dev, RegisterAdress, buffer, 4);

	*value = ((uint32_t)buffer[0] << 24) | ((uint32_t)buffer[1] << 16) |
		 ((uint32_t)buffer[2] << 8) | (uint32_t)buffer[3];
	return status;
}

uint8_t VL53L4CD_RdWord(Dev_t dev, uint16_t RegisterAdress, uint16_t *value)
{
	uint8_t buffer[2];
	uint8_t status = read_register(dev, RegisterAdress, buffer, 2);

	*value = (uint16_t)(((uint16_t)buffer[0] << 8) | buffer[1]);
	return status;
}

uint8_t VL53L4CD_RdByte(Dev_t dev, uint16_t RegisterAdress, uint8_t *value)
{
	return read_register(dev, RegisterAdress, value, 1);
}

uint8_t VL53L4CD_WrByte(Dev_t dev, uint16_t RegisterAdress, uint8_t value)
{
	return write_register(dev, RegisterAdress, &value, 1);
}

uint8_t VL53L4CD_WrWord(Dev_t dev, uint16_t RegisterAdress, uint16_t value)
{
	uint8_t buffer[2] = { (uint8_t)(value >> 8), (uint8_t)value };

	return write_register(dev, RegisterAdress, buffer, 2);
}

uint8_t VL53L4CD_WrDWord(Dev_t dev, uint16_t RegisterAdress, uint32_t value)
{
	uint8_t buffer[4] = { (uint8_t)(value >> 24), (uint8_t)(value >> 16), (uint8_t)(value >> 8),
			      (uint8_t)value };

	return write_register(dev, RegisterAdress, buffer, 4);
}

uint8_t VL53L4CD_WaitMs(Dev_t dev, uint32_t TimeMs)
{
	(void)dev;
	sleep_ms(TimeMs);
	return STATUS_OK;
}
