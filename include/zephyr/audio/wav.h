/*
 * Copyright (c) 2026 Ugo Marchand
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_AUDIO_WAV_H_
#define ZEPHYR_INCLUDE_AUDIO_WAV_H_

/**
 * @file
 * @brief RIFF/WAVE (.wav) container parser.
 */

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include <zephyr/sys/util.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief RIFF/WAVE container parser
 * @defgroup wav WAV
 * @ingroup audio_interface
 * @since 4.5
 * @version 0.1.0
 * @{
 *
 * @details
 * Reads the metadata out of a RIFF/WAVE (`.wav`) container and locates its audio payload,
 * without decoding, converting or playing anything. The payload is handed back verbatim, so
 * the caller decides what to do with it.
 *
 * The container is accessed through a @ref wav_source, a random-access byte source. A
 * read-only memory buffer binding is provided, and an application may supply its own. Parsing
 * reads only a few tens of bytes of header, never the payload, so a large file is never
 * brought into RAM.
 */

/** @brief Smallest container that can be parsed: the `RIFF`/size/`WAVE` preamble. */
#define WAV_MIN_FILE_SIZE 12U

/**
 * @brief Largest single read wav_parse() ever issues.
 *
 * A custom @ref wav_source implementation needing a scratch buffer can size it with this.
 */
#define WAV_SOURCE_MAX_READ_SIZE 40U

/**
 * @brief Read bytes from a WAV container.
 *
 * Copies exactly @p len bytes starting at @p off, an offset from the start of the container,
 * into @p dst. A short read is an error: the parser only ever asks for ranges that lie inside
 * the @ref wav_source::size it was given.
 *
 * @param user_data Opaque pointer from @ref wav_source::user_data.
 * @param off Offset from the start of the container, in bytes.
 * @param dst Destination buffer, at least @p len bytes long.
 * @param len Number of bytes to copy.
 *
 * @retval 0 On success.
 * @retval -errno A negative errno on failure. Propagated to the caller unchanged.
 */
typedef int (*wav_read_cb)(void *user_data, uint32_t off, void *dst, size_t len);

/**
 * @brief A random-access byte source holding a WAV container.
 *
 * Initialize with wav_source_mem_init(), or populate the fields directly to read from
 * somewhere else. The same source is used by wav_parse() and then by wav_read(), so a
 * playback loop is identical for every kind of source.
 */
struct wav_source {
	/** Read callback. Must not be NULL. */
	wav_read_cb read;
	/** Passed to @ref wav_source::read unchanged. */
	void *user_data;
	/**
	 * Total number of bytes the source holds.
	 *
	 * Every size declared by the container is clamped to this, so a truncated or corrupt
	 * file cannot make the parser read out of bounds.
	 */
	uint32_t size;
};

/**
 * @brief Audio sample formats.
 *
 * Only the format tags whose payload is a flat array of fixed-size frames are modelled;
 * anything else is reported as `-ENOTSUP`.
 */
enum wav_format {
	/** Uncompressed integer PCM (`WAVE_FORMAT_PCM`). */
	WAV_FORMAT_PCM = 0x0001,
	/** Uncompressed IEEE 754 floating point (`WAVE_FORMAT_IEEE_FLOAT`). */
	WAV_FORMAT_IEEE_FLOAT = 0x0003,
	/** 8-bit ITU-T G.711 A-law (`WAVE_FORMAT_ALAW`). */
	WAV_FORMAT_ALAW = 0x0006,
	/** 8-bit ITU-T G.711 mu-law (`WAVE_FORMAT_MULAW`). */
	WAV_FORMAT_MULAW = 0x0007,
	/**
	 * Format described by a SubFormat GUID (`WAVE_FORMAT_EXTENSIBLE`).
	 *
	 * Resolved to the effective format while parsing, so wav_parse() never reports it.
	 */
	WAV_FORMAT_EXTENSIBLE = 0xFFFE,
};

/**
 * @brief Everything wav_parse() found out about a container.
 *
 * The values are what the file says. No output-device policy is applied: 24-bit samples,
 * eight channels, 96 kHz and an empty payload all parse successfully, and it is up to the
 * application to reject what its hardware cannot play.
 */
struct wav_info {
	/** Frames per second. Never zero. */
	uint32_t sample_rate;
	/** Offset of the audio payload from the start of the container, in bytes. */
	uint32_t data_off;
	/**
	 * Length of the audio payload, in bytes.
	 *
	 * Clamped to what the source actually holds, so it may be smaller than the length
	 * declared by the `data` chunk.
	 */
	uint32_t data_len;
	/** Number of interleaved channels. Never zero. */
	uint16_t num_channels;
	/** Bits per sample per channel. Never zero, and always a multiple of eight. */
	uint16_t bits_per_sample;
	/** Sample format. Never @ref WAV_FORMAT_EXTENSIBLE. */
	enum wav_format format;
};

/**
 * @brief Initialize a byte source reading from a memory buffer.
 *
 * @p buf must stay valid and unchanged for as long as @p src is used. It is only ever read
 * from.
 *
 * @param src Source to initialize.
 * @param buf Buffer holding the container.
 * @param size Length of @p buf, in bytes.
 *
 * @retval 0 On success.
 * @retval -EINVAL @p src or @p buf is NULL, or @p size exceeds `UINT32_MAX`.
 */
int wav_source_mem_init(struct wav_source *src, const void *buf, size_t size);

/**
 * @brief Parse the header of a WAV container.
 *
 * Walks the RIFF chunk list looking for `fmt ` and `data`, skipping everything else - which
 * matters in practice, because encoders routinely insert a `LIST`/`INFO` chunk and the payload
 * then does not start at the widely quoted offset 44. Reads at most
 * @ref WAV_SOURCE_MAX_READ_SIZE bytes at a time and never touches the payload.
 *
 * @p info is written only on success.
 *
 * @param src Byte source holding the container.
 * @param info Where to store the result.
 *
 * @retval 0 On success.
 * @retval -EINVAL The bytes are not a usable WAV container: NULL argument, source shorter
 *                 than @ref WAV_MIN_FILE_SIZE, missing `RIFF` or `WAVE` marker, truncated
 *                 `fmt ` chunk, or a nonsensical channel count, sample rate or sample width.
 * @retval -ENOENT The container is well formed but has no `fmt ` or no `data` chunk.
 * @retval -ENOTSUP The sample format is valid but not one of @ref wav_format.
 * @retval -E2BIG The chunk list is longer than @kconfig{CONFIG_WAV_MAX_CHUNKS}.
 * @retval -errno Any error reported by @ref wav_source::read, unchanged.
 */
int wav_parse(const struct wav_source *src, struct wav_info *info);

/**
 * @brief Read audio payload.
 *
 * @p off is relative to the start of the payload, so a caller never has to know where in the
 * container it lives. A read running past the end of the payload is clamped rather than
 * refused, so the returned count may be smaller than @p len; zero means the payload is
 * exhausted.
 *
 * Byte-granular: keep @p off and @p len multiples of wav_frame_size() to stay frame-aligned.
 *
 * @param src The same source that was passed to wav_parse().
 * @param info Result of that wav_parse() call.
 * @param off Offset from the start of the payload, in bytes.
 * @param dst Destination buffer, at least @p len bytes long.
 * @param len Maximum number of bytes to read.
 *
 * @retval n The number of bytes copied into @p dst, zero at the end of the payload.
 * @retval -EINVAL NULL argument, or @p off is past the end of the payload.
 * @retval -errno Any error reported by @ref wav_source::read, unchanged.
 */
ssize_t wav_read(const struct wav_source *src, const struct wav_info *info, uint32_t off, void *dst,
		 size_t len);

/**
 * @brief Size of one audio frame, in bytes.
 *
 * A frame holds one sample for each channel, which is the granularity the payload must be
 * split on.
 *
 * @param info Result of a successful wav_parse() call.
 *
 * @return Frame size in bytes. Never zero.
 */
static inline uint32_t wav_frame_size(const struct wav_info *info)
{
	return (uint32_t)info->num_channels * ((uint32_t)info->bits_per_sample / 8U);
}

/**
 * @brief Number of whole audio frames in the payload.
 *
 * @param info Result of a successful wav_parse() call.
 *
 * @return Frame count, zero for an empty payload.
 */
static inline uint32_t wav_frame_count(const struct wav_info *info)
{
	return info->data_len / wav_frame_size(info);
}

/**
 * @brief Playing time of the payload, in milliseconds, rounded down.
 *
 * @param info Result of a successful wav_parse() call.
 *
 * @return Duration in milliseconds, saturated at `UINT32_MAX`.
 */
static inline uint32_t wav_duration_ms(const struct wav_info *info)
{
	uint64_t ms = ((uint64_t)wav_frame_count(info) * 1000U) / info->sample_rate;

	return (ms > UINT32_MAX) ? UINT32_MAX : (uint32_t)ms;
}

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_AUDIO_WAV_H_ */
