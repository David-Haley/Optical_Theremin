/*
 * Optical theremin: polls two VL53L0X time-of-flight sensors at 20 Hz.
 * The pitch sensor (I2C0, GPIO0/GPIO1, header pins 1 and 2) sets the
 * frequency of the DDS tone generator, whose samples are streamed over I2S
 * to a PCM5122 DAC. The volume sensor shares I2C1 (GPIO2/GPIO3, header pins
 * 4 and 5) with the PCM5122 and sets the volume of the DDS tone generator.
 * Readings are not filtered: the DDS generator ramps both the pitch and the
 * volume between readings.
 *
 * At start up the DFR0555 display (on I2C0 with the pitch sensor) shows the program name and
 * build date, then the menu (menu.h), where the rotary encoder (encoder.h)
 * sets the backlight brightness and waveform. Selecting Play starts the
 * theremin, and pushing the encoder returns to the menu. USB serial carries
 * diagnostic and error messages only.
 */

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/clocks.h"

#include "vl53l0x_api.h"
#include "vl53l0x_i2c_platform.h"

#include "build_date.h"
#include "dds_generator.h"
#include "dfr0555_display.h"
#include "encoder.h"
#include "i2s_output.h"
#include "menu.h"
#include "pcm5122.h"

#define VL53L0X_I2C_ADDRESS   0x29
#define PITCH_I2C_BUS         0
#define PITCH_I2C_SPEED_KHZ   100 /* shared with the DFR0555 display, whose LCD drops characters at 400 kHz */
#define VOLUME_I2C_BUS        1 /* shared with the PCM5122, so its speed */
#define POLL_PERIOD_MS        50 /* 20 Hz */
/* PIO_Clock_Divisor and the Phase_Step table assume this system clock. */
#define SYSTEM_CLOCK_HZ       125000000
#define SPLASH_MS             10000 /* minimum time the start-up screen is shown */

/*
 * Longest wait for a measurement, about twice the default 33 ms timing
 * budget.
 */
#define RANGING_TIMEOUT_MS    70

/*
 * Part-to-part range offsets, replacing the factory (NVM) values. The pitch
 * sensor's factory value of 125.5 mm made every reading far too long; it is
 * set so that a flat card at 300 mm reads 300 mm (with no offset it read
 * 316 mm). The device adds the offset to each range. KEEP_FACTORY_OFFSET
 * leaves the NVM value in place.
 */
#define KEEP_FACTORY_OFFSET    0x7FFFFFFF
#define PITCH_RANGE_OFFSET_MM  -16
#define VOLUME_RANGE_OFFSET_MM KEEP_FACTORY_OFFSET

/*
 * The volume sensor's error is not a fixed offset, so its readings are
 * corrected in firmware by a straight line through two measured points: a
 * flat card at VOLUME_CAL_NEAR_MM and VOLUME_CAL_FAR_MM read
 * VOLUME_CAL_NEAR_READ and VOLUME_CAL_FAR_READ, in tenths of a mm, with the
 * factory offset in place. A card at 150 mm read 165.4 mm, 7.4 mm short of
 * the line (about 8 mm, 4 dB, after correction).
 * (VL53L0X_SetLinearityCorrectiveGain can only scale down.)
 */
#define VOLUME_CAL_NEAR_MM   100
#define VOLUME_CAL_NEAR_READ 1262
#define VOLUME_CAL_FAR_MM    300
#define VOLUME_CAL_FAR_READ  3128

/* A sensor and what was last reported about it. */
struct sensor {
	const char *name;
	VL53L0X_Dev_t device;
	VL53L0X_Error last_status;
	uint8_t last_range_status;
};

/* Shows a short message on line 2 of the display (if it works) and stops. */
static void fatal(const char *message)
{
	DFR0555_Put_Line(1, message);
	while (true)
		tight_loop_contents();
}

static void die_on_error(const char *name, const char *step, VL53L0X_Error status)
{
	if (status != VL53L0X_ERROR_NONE) {
		printf("%s %s failed: %d\n", name, step, (int)status);
		if (status == VL53L0X_ERROR_CONTROL_INTERFACE && strcmp(name, "Volume") == 0)
			PCM5122_Scan_Bus();
		char message[DFR0555_Columns + 1];
		snprintf(message, sizeof(message), "%s %s", name, step);
		fatal(message);
	}
}

static void init_sensor(struct sensor *s, uint8_t bus, uint16_t speed_khz, int32_t offset_mm)
{
	VL53L0X_DEV Dev = &s->device;

	memset(Dev, 0, sizeof(*Dev));
	Dev->I2cDevAddr = VL53L0X_I2C_ADDRESS;
	Dev->I2cBus = bus;
	Dev->comms_type = I2C;
	Dev->comms_speed_khz = speed_khz;
	s->last_status = VL53L0X_ERROR_NONE;
	s->last_range_status = 0;

	die_on_error(s->name, "comms init", VL53L0X_comms_initialise(bus, I2C, speed_khz));

	printf("Initialising %s VL53L0X...\n", s->name);

	die_on_error(s->name, "DataInit", VL53L0X_DataInit(Dev));
	die_on_error(s->name, "StaticInit", VL53L0X_StaticInit(Dev));

	uint8_t vhv_settings, phase_cal;
	die_on_error(s->name, "PerformRefCalibration",
		     VL53L0X_PerformRefCalibration(Dev, &vhv_settings, &phase_cal));

	uint32_t ref_spad_count;
	uint8_t is_aperture_spads;
	die_on_error(s->name, "PerformRefSpadManagement",
		     VL53L0X_PerformRefSpadManagement(Dev, &ref_spad_count, &is_aperture_spads));

	int32_t factory_offset_um;
	die_on_error(s->name, "GetOffsetCalibrationDataMicroMeter",
		     VL53L0X_GetOffsetCalibrationDataMicroMeter(Dev, &factory_offset_um));
	printf("%s factory range offset %ld um\n", s->name, (long)factory_offset_um);

	if (offset_mm != KEEP_FACTORY_OFFSET)
		die_on_error(s->name, "SetOffsetCalibrationDataMicroMeter",
			     VL53L0X_SetOffsetCalibrationDataMicroMeter(Dev, offset_mm * 1000));

	die_on_error(s->name, "SetDeviceMode",
		     VL53L0X_SetDeviceMode(Dev, VL53L0X_DEVICEMODE_SINGLE_RANGING));

	printf("%s VL53L0X ready\n", s->name);
}

/*
 * VL53L0X_PerformSingleRangingMeasurement() split in two, so that both
 * sensors (on separate buses) range at the same time: two sequential
 * measurements would take longer than the poll period.
 */
static VL53L0X_Error start_ranging(struct sensor *s)
{
	return VL53L0X_StartMeasurement(&s->device);
}

static VL53L0X_Error finish_ranging(struct sensor *s, VL53L0X_RangingMeasurementData_t *measurement)
{
	VL53L0X_DEV Dev = &s->device;
	absolute_time_t timeout = make_timeout_time_ms(RANGING_TIMEOUT_MS);
	uint8_t ready = 0;
	VL53L0X_Error status;

	while ((status = VL53L0X_GetMeasurementDataReady(Dev, &ready)) == VL53L0X_ERROR_NONE && !ready) {
		if (time_reached(timeout))
			return VL53L0X_ERROR_TIME_OUT;
		VL53L0X_PollingDelay(Dev);
	}
	PALDevDataSet(Dev, PalState, VL53L0X_STATE_IDLE);

	if (status == VL53L0X_ERROR_NONE)
		status = VL53L0X_GetRangingMeasurementData(Dev, measurement);
	if (status == VL53L0X_ERROR_NONE)
		status = VL53L0X_ClearInterruptMask(Dev, 0);
	return status;
}

/*
 * Finishes a measurement and sets *range_mm, returning false if it failed or
 * found nothing in range. Only reports a problem when it changes, so an
 * empty field of view doesn't print a line on every poll.
 */
static bool read_range(struct sensor *s, VL53L0X_Error start_status, int *range_mm)
{
	VL53L0X_RangingMeasurementData_t measurement;
	VL53L0X_Error status = start_status;

	if (status == VL53L0X_ERROR_NONE)
		status = finish_ranging(s, &measurement);
	uint8_t range_status = status == VL53L0X_ERROR_NONE ? measurement.RangeStatus : 0;

	if (status != s->last_status || range_status != s->last_range_status) {
		if (status != VL53L0X_ERROR_NONE)
			printf("%s measurement error: %d\n", s->name, (int)status);
		else if (range_status != 0)
			printf("%s out of range (status %u)\n", s->name, range_status);
		s->last_status = status;
		s->last_range_status = range_status;
	}

	if (status == VL53L0X_ERROR_NONE && range_status == 0) {
		*range_mm = measurement.RangeMilliMeter;
		return true;
	}
	return false;
}

/* Corrects a volume sensor reading, see VOLUME_CAL_NEAR_MM. */
static int correct_volume_range(int raw_mm)
{
	return VOLUME_CAL_NEAR_MM +
	       (raw_mm * 10 - VOLUME_CAL_NEAR_READ) * (VOLUME_CAL_FAR_MM - VOLUME_CAL_NEAR_MM) /
		       (VOLUME_CAL_FAR_READ - VOLUME_CAL_NEAR_READ);
}

/*
 * Plays until the encoder is pushed: ranges both sensors every 50 ms and
 * passes the distances to the DDS generator. Returns with the audio muted.
 */
static void play(const struct settings *settings, struct sensor *pitch, struct sensor *volume)
{
	char range[DFR0555_Columns + 1];
	char line[DFR0555_Columns + 1];

	Range_Name(settings->octave_shift, range, sizeof(range));
	I2S_Output_Set_Waveform(settings->waveform);
	I2S_Output_Set_Octave_Shift(settings->octave_shift);
	DFR0555_Put_Line(0, Waveform_Names[settings->waveform]);
	snprintf(line, sizeof(line), "Range %s", range);
	DFR0555_Put_Line(1, line);
	printf("Play, waveform %s, range %s\n", Waveform_Names[settings->waveform], range);
	Encoder_Flush();

	while (!Encoder_Take_Press()) {
		absolute_time_t next_poll = make_timeout_time_ms(POLL_PERIOD_MS);
		VL53L0X_Error pitch_start = start_ranging(pitch);
		VL53L0X_Error volume_start = start_ranging(volume);

		int pitch_mm;
		if (!read_range(pitch, pitch_start, &pitch_mm))
			pitch_mm = Lowest_Note_MM;
		I2S_Output_Set_Distance(pitch_mm); /* DDS_Generator() clamps it */

		int volume_mm;
		if (read_range(volume, volume_start, &volume_mm))
			volume_mm = correct_volume_range(volume_mm);
		else
			volume_mm = Mute_MM;
		I2S_Output_Set_Volume(volume_mm); /* DDS_Generator() clamps it */

		sleep_until(next_poll);
	}
	I2S_Output_Set_Volume(Mute_MM);
}

int main(void)
{
	stdio_init_all();
	sleep_ms(2000); /* let USB CDC enumerate and the sensors finish booting */

	/* Clears a stuck I2C0 and initialises it, before the display uses it. */
	VL53L0X_comms_initialise(PITCH_I2C_BUS, I2C, PITCH_I2C_SPEED_KHZ);
	struct settings settings = DEFAULT_SETTINGS;
	DFR0555_Init(Brightness_PWM(settings.brightness_level));
	DFR0555_Put_Line(0, "Optical Theremin");
	DFR0555_Put_Line(1, "Built " Build_Date);
	printf("Optical Theremin, built %s\n", Build_Date);
	absolute_time_t splash_end = make_timeout_time_ms(SPLASH_MS);

	uint32_t clk_sys_hz = clock_get_hz(clk_sys);
	if (clk_sys_hz != SYSTEM_CLOCK_HZ) {
		printf("System clock is %lu Hz, not %lu Hz: every note would be out of tune\n",
		       (unsigned long)clk_sys_hz, (unsigned long)SYSTEM_CLOCK_HZ);
		fatal("Clock not 125MHz");
	}

	static struct sensor pitch = { .name = "Pitch" };
	static struct sensor volume = { .name = "Volume" };

	init_sensor(&pitch, PITCH_I2C_BUS, PITCH_I2C_SPEED_KHZ, PITCH_RANGE_OFFSET_MM);
	init_sensor(&volume, VOLUME_I2C_BUS, PCM5122_I2C_Speed_Hz / 1000, VOLUME_RANGE_OFFSET_MM);

	/* BCK/LRCK must be running before the PCM5122 is configured. */
	I2S_Output_Start();

	printf("Initialising PCM5122...\n");
	if (!PCM5122_Init()) {
		printf("PCM5122 init failed\n");
		I2S_Output_Report_Status();
		fatal("PCM5122 failed");
	}
	printf("PCM5122 running\n");

	Encoder_Init();
	sleep_until(splash_end);

	while (true) {
		Menu_Run(&settings);
		play(&settings, &pitch, &volume);
		printf("Menu\n");
	}
}
