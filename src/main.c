/*
 * Optical theremin: polls a VL53L0X time-of-flight sensor (I2C0,
 * GPIO0/GPIO1, header pins 1 and 2) at 20 Hz and uses the distance to set
 * the frequency of the DDS tone generator, whose samples are streamed over
 * I2S to a PCM5122 DAC. USB serial carries diagnostic and error messages
 * only.
 */

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"

#include "vl53l0x_api.h"
#include "vl53l0x_i2c_platform.h"

#include "dds_generator.h"
#include "i2s_output.h"
#include "pcm5122.h"

#define VL53L0X_I2C_ADDRESS   0x29
#define VL53L0X_I2C_SPEED_KHZ 400
#define POLL_PERIOD_MS        50 /* 20 Hz */

static void die_on_error(const char *step, VL53L0X_Error status)
{
	if (status != VL53L0X_ERROR_NONE) {
		printf("%s failed: %d\n", step, (int)status);
		while (true)
			tight_loop_contents();
	}
}

int main(void)
{
	stdio_init_all();
	sleep_ms(2000); /* let USB CDC enumerate and the sensor finish booting */

	VL53L0X_Dev_t device;
	memset(&device, 0, sizeof(device));
	VL53L0X_DEV Dev = &device;

	Dev->I2cDevAddr = VL53L0X_I2C_ADDRESS;
	Dev->comms_type = I2C;
	Dev->comms_speed_khz = VL53L0X_I2C_SPEED_KHZ;

	die_on_error("comms init", VL53L0X_comms_initialise(I2C, VL53L0X_I2C_SPEED_KHZ));

	printf("Initialising VL53L0X...\n");

	die_on_error("DataInit", VL53L0X_DataInit(Dev));
	die_on_error("StaticInit", VL53L0X_StaticInit(Dev));

	uint8_t vhv_settings, phase_cal;
	die_on_error("PerformRefCalibration",
		     VL53L0X_PerformRefCalibration(Dev, &vhv_settings, &phase_cal));

	uint32_t ref_spad_count;
	uint8_t is_aperture_spads;
	die_on_error("PerformRefSpadManagement",
		     VL53L0X_PerformRefSpadManagement(Dev, &ref_spad_count, &is_aperture_spads));

	die_on_error("SetDeviceMode",
		     VL53L0X_SetDeviceMode(Dev, VL53L0X_DEVICEMODE_SINGLE_RANGING));

	printf("VL53L0X ready\n");

	/* BCK/LRCK must be running before the PCM5122 is configured. */
	I2S_Output_Start();

	printf("Initialising PCM5122...\n");
	if (!PCM5122_Init()) {
		printf("PCM5122 init failed\n");
		I2S_Output_Report_Status();
		while (true)
			tight_loop_contents();
	}
	printf("PCM5122 running\n");

	/*
	 * Only report a problem when it changes, so an empty field of view
	 * doesn't print a line on every poll.
	 */
	VL53L0X_Error last_status = VL53L0X_ERROR_NONE;
	uint8_t last_range_status = 0;

	while (true) {
		absolute_time_t next_poll = make_timeout_time_ms(POLL_PERIOD_MS);
		VL53L0X_RangingMeasurementData_t measurement;
		VL53L0X_Error status = VL53L0X_PerformSingleRangingMeasurement(Dev, &measurement);
		uint8_t range_status = status == VL53L0X_ERROR_NONE ? measurement.RangeStatus : 0;

		if (status == VL53L0X_ERROR_NONE && range_status == 0)
			I2S_Output_Set_Distance(measurement.RangeMilliMeter);
		else
			I2S_Output_Set_Distance(Lowest_Note_MM);

		if (status != last_status || range_status != last_range_status) {
			if (status != VL53L0X_ERROR_NONE)
				printf("Measurement error: %d\n", (int)status);
			else if (range_status != 0)
				printf("Out of range (status %u)\n", range_status);
			last_status = status;
			last_range_status = range_status;
		}

		sleep_until(next_poll);
	}
}
