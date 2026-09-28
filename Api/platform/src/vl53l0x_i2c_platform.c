/*
 * Raspberry Pi Pico platform I2C layer for the ST VL53L0X API.
 *
 * Replaces the original Windows/ranging_sensor_comms.dll backed
 * implementation with the Pico SDK hardware_i2c driver. The bus argument
 * selects the I2C controller:
 *   0 - I2C0 on GPIO0 (SDA) / GPIO1 (SCL), physical header pins 1 and 2
 *   1 - I2C1 on GPIO2 (SDA) / GPIO3 (SCL), physical header pins 4 and 5
 */

#include <stdio.h>
#include <string.h>

#include "hardware/i2c.h"
#include "hardware/gpio.h"
#include "pico/time.h"

#include "vl53l0x_i2c_platform.h"
#include "vl53l0x_def.h"

#define STATUS_OK    0x00
#define STATUS_FAIL  0x01

struct i2c_bus {
	i2c_inst_t *instance;
	uint sda_pin;
	uint scl_pin;
};

static const struct i2c_bus buses[] = {
	{ i2c0, 0, 1 },
	{ i2c1, 2, 3 },
};

#define BUS_COUNT (sizeof(buses) / sizeof(buses[0]))

/*
 * Per-transfer timeout, so a bus held low returns an error instead of
 * blocking forever. A 64 byte transfer at 100 kHz takes about 6 ms.
 */
#define TRANSFER_TIMEOUT_US 20000

#define RECOVERY_HALF_PERIOD_US 5 /* 100 kHz */

/*
 * If the Pico is reset in the middle of a read, the sensor can be left
 * driving SDA low, waiting for the clocks to finish its byte, and the bus
 * stays stuck through every later reset. Clock SCL (open drain, by
 * switching the pin between output low and input) until SDA is released,
 * at most 9 times, then send a STOP.
 */
static void recover_bus(const struct i2c_bus *b, uint8_t bus)
{
	gpio_init(b->sda_pin);
	gpio_init(b->scl_pin);
	gpio_pull_up(b->sda_pin);
	gpio_pull_up(b->scl_pin);
	gpio_put(b->sda_pin, 0);
	gpio_put(b->scl_pin, 0);
	sleep_us(RECOVERY_HALF_PERIOD_US);

	if (gpio_get(b->sda_pin))
		return;

	int pulses = 0;
	while (!gpio_get(b->sda_pin) && pulses < 9) {
		gpio_set_dir(b->scl_pin, GPIO_OUT); /* SCL low */
		sleep_us(RECOVERY_HALF_PERIOD_US);
		gpio_set_dir(b->scl_pin, GPIO_IN); /* SCL released high */
		sleep_us(RECOVERY_HALF_PERIOD_US);
		pulses++;
	}

	/* STOP: SDA rises while SCL is high. */
	gpio_set_dir(b->scl_pin, GPIO_OUT);
	gpio_set_dir(b->sda_pin, GPIO_OUT);
	sleep_us(RECOVERY_HALF_PERIOD_US);
	gpio_set_dir(b->scl_pin, GPIO_IN);
	sleep_us(RECOVERY_HALF_PERIOD_US);
	gpio_set_dir(b->sda_pin, GPIO_IN);
	sleep_us(RECOVERY_HALF_PERIOD_US);

	printf("I2C%u SDA was held low: %d clock pulses, SDA now %d\n",
	       bus, pulses, gpio_get(b->sda_pin));
}

int32_t VL53L0X_comms_initialise(uint8_t bus, uint8_t comms_type, uint16_t comms_speed_khz)
{
	if (bus >= BUS_COUNT || comms_type != I2C)
		return STATUS_FAIL;

	recover_bus(&buses[bus], bus);

	i2c_init(buses[bus].instance, (uint)comms_speed_khz * 1000);

	gpio_set_function(buses[bus].sda_pin, GPIO_FUNC_I2C);
	gpio_set_function(buses[bus].scl_pin, GPIO_FUNC_I2C);
	gpio_pull_up(buses[bus].sda_pin);
	gpio_pull_up(buses[bus].scl_pin);

	return STATUS_OK;
}

int32_t VL53L0X_comms_close(uint8_t bus)
{
	if (bus >= BUS_COUNT)
		return STATUS_FAIL;

	i2c_deinit(buses[bus].instance);
	return STATUS_OK;
}

int32_t VL53L0X_write_multi(uint8_t bus, uint8_t address, uint8_t index, uint8_t *pdata, int32_t count)
{
	uint8_t buffer[COMMS_BUFFER_SIZE + 1];

	if (bus >= BUS_COUNT || count > COMMS_BUFFER_SIZE)
		return STATUS_FAIL;

	buffer[0] = index;
	memcpy(&buffer[1], pdata, (size_t)count);

	int written = i2c_write_timeout_us(buses[bus].instance, address, buffer, (size_t)count + 1, false,
					   TRANSFER_TIMEOUT_US);

	return (written == count + 1) ? STATUS_OK : STATUS_FAIL;
}

int32_t VL53L0X_read_multi(uint8_t bus, uint8_t address, uint8_t index, uint8_t *pdata, int32_t count)
{
	if (bus >= BUS_COUNT)
		return STATUS_FAIL;

	int written = i2c_write_timeout_us(buses[bus].instance, address, &index, 1, true, TRANSFER_TIMEOUT_US);
	if (written != 1)
		return STATUS_FAIL;

	int read = i2c_read_timeout_us(buses[bus].instance, address, pdata, (size_t)count, false,
				      TRANSFER_TIMEOUT_US);

	return (read == count) ? STATUS_OK : STATUS_FAIL;
}

int32_t VL53L0X_write_byte(uint8_t bus, uint8_t address, uint8_t index, uint8_t data)
{
	return VL53L0X_write_multi(bus, address, index, &data, 1);
}

int32_t VL53L0X_write_word(uint8_t bus, uint8_t address, uint8_t index, uint16_t data)
{
	uint8_t buffer[BYTES_PER_WORD];

	buffer[0] = (uint8_t)(data >> 8);
	buffer[1] = (uint8_t)(data & 0x00FF);

	return VL53L0X_write_multi(bus, address, index, buffer, BYTES_PER_WORD);
}

int32_t VL53L0X_write_dword(uint8_t bus, uint8_t address, uint8_t index, uint32_t data)
{
	uint8_t buffer[BYTES_PER_DWORD];

	buffer[0] = (uint8_t)(data >> 24);
	buffer[1] = (uint8_t)((data & 0x00FF0000) >> 16);
	buffer[2] = (uint8_t)((data & 0x0000FF00) >> 8);
	buffer[3] = (uint8_t)(data & 0x000000FF);

	return VL53L0X_write_multi(bus, address, index, buffer, BYTES_PER_DWORD);
}

int32_t VL53L0X_read_byte(uint8_t bus, uint8_t address, uint8_t index, uint8_t *pdata)
{
	return VL53L0X_read_multi(bus, address, index, pdata, 1);
}

int32_t VL53L0X_read_word(uint8_t bus, uint8_t address, uint8_t index, uint16_t *pdata)
{
	uint8_t buffer[BYTES_PER_WORD];
	int32_t status = VL53L0X_read_multi(bus, address, index, buffer, BYTES_PER_WORD);

	*pdata = ((uint16_t)buffer[0] << 8) + (uint16_t)buffer[1];

	return status;
}

int32_t VL53L0X_read_dword(uint8_t bus, uint8_t address, uint8_t index, uint32_t *pdata)
{
	uint8_t buffer[BYTES_PER_DWORD];
	int32_t status = VL53L0X_read_multi(bus, address, index, buffer, BYTES_PER_DWORD);

	*pdata = ((uint32_t)buffer[0] << 24) + ((uint32_t)buffer[1] << 16) +
		 ((uint32_t)buffer[2] << 8) + (uint32_t)buffer[3];

	return status;
}

int32_t VL53L0X_platform_wait_us(int32_t wait_us)
{
	sleep_us((uint64_t)wait_us);
	return STATUS_OK;
}

int32_t VL53L0X_wait_ms(int32_t wait_ms)
{
	sleep_ms((uint32_t)wait_ms);
	return STATUS_OK;
}

int32_t VL53L0X_set_gpio(uint8_t level)
{
	(void)level;
	return STATUS_OK;
}

int32_t VL53L0X_get_gpio(uint8_t *plevel)
{
	*plevel = 0;
	return STATUS_OK;
}

int32_t VL53L0X_release_gpio(void)
{
	return STATUS_OK;
}

int32_t VL53L0X_cycle_power(void)
{
	return STATUS_OK;
}

int32_t VL53L0X_get_timer_frequency(int32_t *ptimer_freq_hz)
{
	*ptimer_freq_hz = 1000000;
	return STATUS_OK;
}

int32_t VL53L0X_get_timer_value(int32_t *ptimer_count)
{
	*ptimer_count = (int32_t)time_us_64();
	return STATUS_OK;
}
