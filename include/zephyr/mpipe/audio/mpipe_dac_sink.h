/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Audio codec sink element.
 * @ingroup mpipe_audio_dac_sink
 *
 * The dac_sink element plays the PCM buffers reaching the end of a pipeline
 * through a Zephyr audio codec device. The codec paces the stream: the
 * element hands it one buffer per free block and blocks until the codec
 * reports the next one free, so backpressure reaches the upstream queue.
 */

#ifndef ZEPHYR_INCLUDE_MPIPE_AUDIO_MPIPE_DAC_SINK_H_
#define ZEPHYR_INCLUDE_MPIPE_AUDIO_MPIPE_DAC_SINK_H_

/**
 * @defgroup mpipe_audio_dac_sink Audio codec sinks
 * @ingroup mpipe_audio
 * @brief Element playing pipeline buffers through an audio codec.
 *
 * @{
 */

#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>

#include <zephyr/mpipe/mpipe_element.h>
#include <zephyr/mpipe/mpipe_sink.h>
#include <zephyr/mpipe/mpipe_structure.h>

/**
 * @brief Audio codec sink property identifiers
 */
enum {
	/**
	 * Codec device to play through, a const struct device pointer.
	 * Must be set before the element leaves the NULL state.
	 */
	MPIPE_PROP_AUDIO_DAC_SINK_CODEC = MPIPE_PROP_SINK_LAST,
	/**
	 * Capability this sink accepts, a const struct mpipe_structure pointer,
	 * copied. Defaults to mono 16-bit PCM over the whole rate range.
	 */
	MPIPE_PROP_AUDIO_DAC_SINK_CAPS,
};

/**
 * @brief Audio codec sink element structure
 */
struct mpipe_dac_sink {
	/** Base sink element (must be first) */
	struct mpipe_sink sink;
	/** Codec device the buffers are written to */
	const struct device *codec;
	/** Capability the sink accepts, offered at every negotiation */
	struct mpipe_structure caps;
	/** Given once per free codec block, taken before each write */
	struct k_sem block_free;
	/** Whether the codec output is running */
	bool playing;
};

/**
 * @brief Initialize an audio codec sink element
 *
 * @param dac_sink Pointer to the element to initialize.
 * @param id Unique element identifier.
 *
 * @return 0 on success, negative errno otherwise.
 */
int mpipe_dac_sink_init(struct mpipe_dac_sink *dac_sink, uint8_t id);

/** @} */

#endif /* ZEPHYR_INCLUDE_MPIPE_AUDIO_MPIPE_DAC_SINK_H_ */
