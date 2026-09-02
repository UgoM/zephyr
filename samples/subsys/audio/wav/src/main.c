/*
 * Copyright (c) 2026 Ugo Marchand
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Parse a .wav file that has been baked into flash, report what it holds, and stream its
 * payload through in fixed-size blocks the way a playback loop would.
 *
 * Nothing here knows the sample rate, the channel count or where the audio starts: those all
 * come out of the container's header at run time. Point the build at a different clip and the
 * numbers below change on their own.
 */

#include <stdint.h>
#include <stdlib.h>

#include <zephyr/audio/wav.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

/** One refill, in bytes. A real player would hand a block this size to a codec driver. */
#define BLOCK_SIZE 256

/* The .wav file, byte for byte, header included. Generated at build time by
 * scripts/gen_clip.py and turned into an array by generate_inc_file_for_target().
 */
static const uint8_t clip[] = {
#include "clip.inc"
};

static uint8_t block[BLOCK_SIZE];

static const char *format_name(enum wav_format format)
{
	switch (format) {
	case WAV_FORMAT_PCM:
		return "PCM";
	case WAV_FORMAT_IEEE_FLOAT:
		return "IEEE float";
	case WAV_FORMAT_ALAW:
		return "A-law";
	case WAV_FORMAT_MULAW:
		return "mu-law";
	default:
		return "unknown";
	}
}

int main(void)
{
	struct wav_source src;
	struct wav_info info;
	uint32_t off = 0;
	unsigned int blocks = 0;
	int16_t peak = 0;
	int ret;

	ret = wav_source_mem_init(&src, clip, sizeof(clip));
	if (ret < 0) {
		printk("wav_source_mem_init() failed: %d\n", ret);
		return ret;
	}

	ret = wav_parse(&src, &info);
	if (ret < 0) {
		printk("wav_parse() failed: %d\n", ret);
		return ret;
	}

	printk("%s, %u channel(s), %u Hz, %u-bit\n", format_name(info.format), info.num_channels,
	       info.sample_rate, info.bits_per_sample);
	printk("payload: %u bytes at offset %u, %u frames of %u bytes, %u ms\n", info.data_len,
	       info.data_off, wav_frame_count(&info), wav_frame_size(&info),
	       wav_duration_ms(&info));

	/*
	 * The library reports whatever the file says; deciding what is playable is the
	 * application's job, because only it knows what its output hardware accepts.
	 */
	if ((info.format != WAV_FORMAT_PCM) || (info.bits_per_sample != 16)) {
		printk("this sample only measures 16-bit PCM\n");
		return 0;
	}

	/*
	 * Read the payload the way a player consumes it: a block at a time, in whole frames,
	 * until wav_read() reports there is nothing left. A player would hand each block to a
	 * codec instead of measuring it.
	 */
	while (true) {
		ssize_t got = wav_read(&src, &info, off, block,
				       ROUND_DOWN(BLOCK_SIZE, wav_frame_size(&info)));

		if (got < 0) {
			printk("wav_read() failed at offset %u: %d\n", off, (int)got);
			return (int)got;
		}

		if (got == 0) {
			break;
		}

		for (size_t i = 0; (i + 1U) < (size_t)got; i += 2U) {
			int16_t sample = (int16_t)sys_get_le16(&block[i]);
			int16_t magnitude =
				(sample == INT16_MIN) ? INT16_MAX : (int16_t)abs(sample);

			peak = MAX(peak, magnitude);
		}

		off += (uint32_t)got;
		blocks++;
	}

	printk("streamed %u bytes in %u blocks, peak amplitude %d/%d\n", off, blocks, peak,
	       INT16_MAX);
	printk("sample finished\n");

	return 0;
}
