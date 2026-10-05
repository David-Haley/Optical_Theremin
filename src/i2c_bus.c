// This file implements initialisation of the two I2C controllers,
// including recovery of a bus left stuck by a reset mid-transfer.
// Moved from the VL53L0X porting layer (stage 10).
// Author : David Haley

#include <stdio.h>

#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "pico/time.h"

#include "i2c_bus.h"

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

bool I2C_Bus_Init(uint8_t bus, uint16_t speed_khz)
{
	if (bus >= BUS_COUNT)
		return false;

	recover_bus(&buses[bus], bus);

	i2c_init(buses[bus].instance, (uint)speed_khz * 1000);

	gpio_set_function(buses[bus].sda_pin, GPIO_FUNC_I2C);
	gpio_set_function(buses[bus].scl_pin, GPIO_FUNC_I2C);
	gpio_pull_up(buses[bus].sda_pin);
	gpio_pull_up(buses[bus].scl_pin);

	return true;
}

i2c_inst_t *I2C_Bus_Instance(uint8_t bus)
{
	return bus < BUS_COUNT ? buses[bus].instance : NULL;
}
