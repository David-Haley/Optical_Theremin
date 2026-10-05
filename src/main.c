/*
 * Optical theremin: reads two VL53L4CD time-of-flight sensors at 20 Hz.
 * The pitch sensor (I2C0, GPIO0/GPIO1, header pins 1 and 2) sets the
 * frequency of the DDS tone generator, whose samples are streamed over I2S
 * to a PCM5122 DAC. The volume sensor shares I2C1 (GPIO2/GPIO3, header pins
 * 4 and 5) with the PCM5122 and sets the volume of the DDS tone generator.
 * Readings are not filtered: the DDS generator ramps both the pitch and the
 * volume between readings.
 *
 * At start up the DFR0555 display (on I2C0 with the pitch sensor) shows the program name and
 * build date, then the menu (menu.h), where the rotary encoder (encoder.h)
 * sets the backlight brightness, waveform and octave range. Selecting Play starts the
 * theremin, and pushing the encoder returns to the menu. USB serial carries
 * diagnostic and error messages only.
 */

#include <stdio.h>

#include "pico/stdlib.h"
#include "hardware/clocks.h"

#include "VL53L4CD_api.h"

#include "build_date.h"
#include "dds_generator.h"
#include "dfr0555_display.h"
#include "encoder.h"
#include "i2c_bus.h"
#include "i2s_output.h"
#include "menu.h"
#include "pcm5122.h"

#define VL53L4CD_SENSOR_ID    0xEBAA

/*
 * Writing 0 to the soft reset register holds the sensor in reset, and
 * writing 1 releases it. XSHUT isn't wired, so this is the only way to
 * start from a known state: a sensor keeps its state through a Pico reset,
 * and the VL53L0X firmware once left one held in reset (its ID read 0).
 * The ULD's own VL53L4CD_SOFT_RESET has a stray parenthesis.
 */
#define SOFT_RESET_REGISTER   0x0000
#define SOFT_RESET_HOLD_MS    1
#define BOOT_MS               2 /* tBOOT is 1.2 ms maximum */
#define BOOT_TIMEOUT_MS       100
#define FIRMWARE_SYSTEM_STATUS 0x00E5 /* VL53L4CD_FIRMWARE__SYSTEM_STATUS */
#define FIRMWARE_BOOTED       0x03
#define PITCH_I2C_BUS         0
#define PITCH_I2C_SPEED_KHZ   100 /* shared with the DFR0555 display, whose LCD drops characters at 400 kHz */
#define VOLUME_I2C_BUS        1 /* shared with the PCM5122, so its speed */
#define POLL_PERIOD_MS        50 /* 20 Hz */
/* PIO_Clock_Divisor and the Phase_Step table assume this system clock. */
#define SYSTEM_CLOCK_HZ       125000000
#define SPLASH_MS             10000 /* minimum time the start-up screen is shown */

/*
 * Each sensor ranges on its own, one measurement every POLL_PERIOD_MS,
 * with the laser on for RANGING_BUDGET_MS of it (the budget must be
 * shorter than the period).
 */
#define RANGING_BUDGET_MS     40

/*
 * Longest wait for a measurement. The sensors aren't calibrated yet
 * (stage 10): readings are used as they come.
 */
#define RANGING_TIMEOUT_MS    70

/* A sensor and what was last reported about it. */
struct sensor {
	const char *name;
	Dev_t device;
	VL53L4CD_Error last_status;
	uint8_t last_range_status;
};

/* Shows a short message on line 2 of the display (if it works) and stops. */
static void fatal(const char *message)
{
	DFR0555_Put_Line(1, message);
	while (true)
		tight_loop_contents();
}

static void die_on_error(const char *name, const char *step, VL53L4CD_Error status)
{
	if (status != VL53L4CD_ERROR_NONE) {
		printf("%s %s failed: %d\n", name, step, (int)status);
		char message[DFR0555_Columns + 1];
		snprintf(message, sizeof(message), "%s %s", name, step);
		fatal(message);
	}
}

static void init_sensor(struct sensor *s, uint8_t bus, uint16_t speed_khz)
{
	Dev_t dev = VL53L4CD_Dev(bus);

	s->device = dev;
	s->last_status = VL53L4CD_ERROR_NONE;
	s->last_range_status = 0;

	if (!I2C_Bus_Init(bus, speed_khz))
		die_on_error(s->name, "bus init", VL53L4CD_ERROR_INVALID_ARGUMENT);

	printf("Initialising %s VL53L4CD...\n", s->name);

	VL53L4CD_Error status = VL53L4CD_WrByte(dev, SOFT_RESET_REGISTER, 0);
	if (status == VL53L4CD_ERROR_NONE) {
		sleep_ms(SOFT_RESET_HOLD_MS);
		status = VL53L4CD_WrByte(dev, SOFT_RESET_REGISTER, 1);
	}
	sleep_ms(BOOT_MS);
	if (status != VL53L4CD_ERROR_NONE && bus == VOLUME_I2C_BUS)
		PCM5122_Scan_Bus();
	die_on_error(s->name, "soft reset", status);

	/*
	 * Waits for the sensor to boot. Accessing it during tBOOT left it
	 * NACKing (and reporting itself booted), hence BOOT_MS first.
	 * VL53L4CD_SensorInit waits too, but treats any failed transfer as
	 * fatal.
	 */
	absolute_time_t boot_timeout = make_timeout_time_ms(BOOT_TIMEOUT_MS);
	uint8_t system_status;
	while (((status = VL53L4CD_RdByte(dev, FIRMWARE_SYSTEM_STATUS, &system_status)) != VL53L4CD_ERROR_NONE ||
		system_status != FIRMWARE_BOOTED) &&
	       !time_reached(boot_timeout))
		sleep_ms(1);
	if (status == VL53L4CD_ERROR_NONE && system_status != FIRMWARE_BOOTED)
		status = VL53L4CD_ERROR_TIMEOUT;
	if (status != VL53L4CD_ERROR_NONE && bus == VOLUME_I2C_BUS)
		PCM5122_Scan_Bus();
	die_on_error(s->name, "boot", status);

	uint16_t id;
	status = VL53L4CD_GetSensorId(dev, &id);
	if (status == VL53L4CD_ERROR_NONE && id != VL53L4CD_SENSOR_ID) {
		printf("%s sensor ID 0x%04X, not 0x%04X\n", s->name, id, VL53L4CD_SENSOR_ID);
		status = VL53L4CD_ERROR_INVALID_ARGUMENT;
	}
	if (status != VL53L4CD_ERROR_NONE && bus == VOLUME_I2C_BUS)
		PCM5122_Scan_Bus();
	die_on_error(s->name, "sensor ID", status);

	die_on_error(s->name, "SensorInit", VL53L4CD_SensorInit(dev));
	die_on_error(s->name, "SetRangeTiming",
		     VL53L4CD_SetRangeTiming(dev, RANGING_BUDGET_MS, POLL_PERIOD_MS));
	die_on_error(s->name, "StartRanging", VL53L4CD_StartRanging(dev));

	printf("%s VL53L4CD ready\n", s->name);
}

/*
 * Waits for the sensor's next measurement and sets *range_mm, returning
 * false if it failed or found nothing in range. Only reports a problem when
 * it changes, so an empty field of view doesn't print a line on every poll.
 */
static bool read_range(struct sensor *s, int *range_mm)
{
	VL53L4CD_ResultsData_t result;
	absolute_time_t timeout = make_timeout_time_ms(RANGING_TIMEOUT_MS);
	uint8_t ready = 0;
	VL53L4CD_Error status;

	while ((status = VL53L4CD_CheckForDataReady(s->device, &ready)) == VL53L4CD_ERROR_NONE && !ready) {
		if (time_reached(timeout)) {
			status = VL53L4CD_ERROR_TIMEOUT;
			break;
		}
		sleep_ms(1);
	}

	if (status == VL53L4CD_ERROR_NONE)
		status = VL53L4CD_GetResult(s->device, &result);
	if (status == VL53L4CD_ERROR_NONE)
		status = VL53L4CD_ClearInterrupt(s->device);
	uint8_t range_status = status == VL53L4CD_ERROR_NONE ? result.range_status : 0;

	if (status != s->last_status || range_status != s->last_range_status) {
		if (status != VL53L4CD_ERROR_NONE)
			printf("%s measurement error: %d\n", s->name, (int)status);
		else if (range_status != 0)
			printf("%s out of range (status %u)\n", s->name, range_status);
		s->last_status = status;
		s->last_range_status = range_status;
	}

	if (status == VL53L4CD_ERROR_NONE && range_status == 0) {
		*range_mm = result.distance_mm;
		return true;
	}
	return false;
}

/*
 * Plays until the encoder is pushed: ranges both sensors every 50 ms and
 * passes the distances to the DDS generator. Returns with the audio muted.
 */
static void play(const struct settings *settings, struct sensor *pitch, struct sensor *volume)
{
	char range[DFR0555_Columns + 1];
	char line[DFR0555_Columns + 1];

	Range_Name(settings->octave_range, range, sizeof(range));
	I2S_Output_Set_Waveform(settings->waveform);
	I2S_Output_Set_Octave_Range(settings->octave_range);
	DFR0555_Put_Line(0, Waveform_Names[settings->waveform]);
	snprintf(line, sizeof(line), "Range %s", range);
	DFR0555_Put_Line(1, line);
	printf("Play, waveform %s, range %s\n", Waveform_Names[settings->waveform], range);
	Encoder_Flush();

	/*
	 * The sensors kept ranging in the menu; discard what they measured
	 * there, so the first readings are fresh.
	 */
	VL53L4CD_ClearInterrupt(pitch->device);
	VL53L4CD_ClearInterrupt(volume->device);

	while (!Encoder_Take_Press()) {
		absolute_time_t next_poll = make_timeout_time_ms(POLL_PERIOD_MS);

		int pitch_mm;
		if (!read_range(pitch, &pitch_mm))
			pitch_mm = Lowest_Note_MM;
		I2S_Output_Set_Distance(pitch_mm); /* DDS_Generator() clamps it */

		int volume_mm;
		if (!read_range(volume, &volume_mm))
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
	I2C_Bus_Init(PITCH_I2C_BUS, PITCH_I2C_SPEED_KHZ);
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

	init_sensor(&pitch, PITCH_I2C_BUS, PITCH_I2C_SPEED_KHZ);
	init_sensor(&volume, VOLUME_I2C_BUS, PCM5122_I2C_Speed_Hz / 1000);

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
