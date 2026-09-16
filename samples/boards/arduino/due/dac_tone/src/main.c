/*
 * Copyright (c) 2026 Arduino
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/dac.h>
#include <zephyr/sys/printk.h>
#include <soc.h>

#define DAC_CHANNEL 0U
#define DAC_RESOLUTION 12U

#define SAMPLE_RATE 48000U
#define TONE_FREQ 440U
#define TABLE_LEN 256U

/* TONE_FREQ * 2^32 / SAMPLE_RATE: phase advance per sample */
#define PHASE_INCR 39370496UL

static const struct device *dac_dev = DEVICE_DT_GET(DT_ALIAS(dac_0));
static uint16_t sine_table[TABLE_LEN];
static uint32_t phase;

static void build_sine_table(void)
{
	for (int i = 0; i < TABLE_LEN; i++) {
		double v = 2048.0 + 1792.0 * __builtin_sin(2 * 3.141592653589793
							   * i / TABLE_LEN);
		sine_table[i] = (uint16_t)v;
	}
}

/* Start the DWT data witness cycle counter as an MCK-rate timebase.
 * It is a single memory-mapped 32-bit counter readable without the
 * overhead of k_cycle_get_32().
 */
static void dwt_start(void)
{
	CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
	DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
	DWT->CYCCNT = 0U;
}

int main(void)
{
	const struct dac_channel_cfg cfg = {
		.channel_id = DAC_CHANNEL,
		.resolution = DAC_RESOLUTION,
		.buffered = true,
	};
	uint32_t cycles_per_sec;
	uint32_t sample_period;
	uint32_t start_cyc;
	int ret;

	if (!device_is_ready(dac_dev)) {
		printk("%s: device not ready\n", dac_dev->name);
		return 0;
	}

	ret = dac_channel_setup(dac_dev, &cfg);
	if (ret != 0) {
		printk("dac_channel_setup failed: %d\n", ret);
		return 0;
	}

	build_sine_table();
	printk("Playing %d Hz tone on DAC%d at %d Hz sample rate\n",
	       TONE_FREQ, DAC_CHANNEL, SAMPLE_RATE);

	cycles_per_sec = sys_clock_hw_cycles_per_sec();
	sample_period = cycles_per_sec / SAMPLE_RATE;
	dwt_start();
	start_cyc = DWT->CYCCNT;

	while (1) {
		uint16_t value;

		phase += PHASE_INCR;
		value = sine_table[(phase >> 24) & (TABLE_LEN - 1)];

		ret = dac_write_value(dac_dev, DAC_CHANNEL, value);
		if (ret != 0) {
			printk("dac_write_value failed: %d\n", ret);
			return 0;
		}

		/* Spin until the deadline, advancing it by exactly one
		 * sample period so no cycle drift accumulates.
		 */
		while ((DWT->CYCCNT - start_cyc) < sample_period) {
		}
		start_cyc += sample_period;
	}
	return 0;
}
