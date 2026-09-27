/*
 * Optical theremin: polls a VL53L0X time-of-flight sensor (I2C0,
 * GPIO0/GPIO1, header pins 1 and 2) at 20 Hz and uses the filtered distance
 * to set the frequency of the DDS tone generator, whose samples are streamed
 * over I2S to a PCM5122 DAC. The waveform is selected by grounding one of
 * header pins 14 to 17 (see waveform_select.h). USB serial carries
 * diagnostic and error messages only.
 */

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"

#include "vl53l0x_api.h"
#include "vl53l0x_i2c_platform.h"

#include "dds_generator.h"
#include "i2s_output.h"
#include "pcm5122.h"
#include "waveform_select.h"

#define VL53L0X_I2C_ADDRESS   0x29
#define VL53L0X_I2C_SPEED_KHZ 400
#define POLL_PERIOD_MS        50 /* 20 Hz */

/*
 * Distance filter: a median of the last MEDIAN_LENGTH readings rejects
 * isolated spikes and dropouts, then an exponential moving average with
 * alpha = 1 / 2^SMOOTHING_SHIFT smooths the remaining jitter. Larger values
 * are steadier but slower to follow the hand: at 20 Hz, shift 1 lags about
 * 100 ms and shift 2 about 200 ms.
 */
#define MEDIAN_LENGTH   3
#define SMOOTHING_SHIFT 1
#define FILTER_FRACTION 4 /* fractional bits kept by the average */

static const char *const waveform_names[] = { "Sine", "Sine_2", "Triangle", "Square" };

static void die_on_error(const char *step, VL53L0X_Error status)
{
	if (status != VL53L0X_ERROR_NONE) {
		printf("%s failed: %d\n", step, (int)status);
		while (true)
			tight_loop_contents();
	}
}

static int median3(int a, int b, int c)
{
	if (a > b) {
		int t = a;
		a = b;
		b = t;
	}
	/* now a <= b */
	if (c < a)
		return a;
	if (c > b)
		return b;
	return c;
}

_Static_assert(MEDIAN_LENGTH == 3, "median3 expects MEDIAN_LENGTH == 3");

static int filter_distance(int raw)
{
	static int history[MEDIAN_LENGTH] = { Lowest_Note_MM, Lowest_Note_MM, Lowest_Note_MM };
	static int next;
	static int filtered = Lowest_Note_MM << FILTER_FRACTION;

	/* Keep far out-of-range readings from dragging the average. */
	if (raw < Highest_Note_MM)
		raw = Highest_Note_MM;
	else if (raw > Lowest_Note_MM)
		raw = Lowest_Note_MM;

	history[next] = raw;
	next = (next + 1) % MEDIAN_LENGTH;

	int median = median3(history[0], history[1], history[2]);
	filtered += ((median << FILTER_FRACTION) - filtered) >> SMOOTHING_SHIFT;
	return (filtered + (1 << (FILTER_FRACTION - 1))) >> FILTER_FRACTION;
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

	Waveform_Select_Init();
	Waveforms waveform = Waveform_Select_Read();
	I2S_Output_Set_Waveform(waveform);
	printf("Waveform %s\n", waveform_names[waveform]);

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
			I2S_Output_Set_Distance(filter_distance(measurement.RangeMilliMeter));
		else
			I2S_Output_Set_Distance(filter_distance(Lowest_Note_MM));

		Waveforms selected = Waveform_Select_Read();
		if (selected != waveform) {
			waveform = selected;
			I2S_Output_Set_Waveform(waveform);
			printf("Waveform %s\n", waveform_names[waveform]);
		}

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
