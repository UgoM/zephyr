/*
 * Copyright (c) 2026 Ugo Marchand
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/audio/wav.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(wav, CONFIG_WAV_LOG_LEVEL);

/** Size of a RIFF chunk header: four-character identifier plus a 32-bit size. */
#define WAV_CHUNK_HEADER_SIZE 8U

/** Smallest `fmt ` chunk body: everything up to and including the sample width. */
#define WAV_FMT_MIN_SIZE 16U

/** Largest `fmt ` chunk body we care about: the `WAVE_FORMAT_EXTENSIBLE` flavour. */
#define WAV_FMT_EXT_SIZE 40U

/* Byte offsets of the fields we read inside the body of a `fmt ` chunk. */
#define WAV_FMT_OFF_TAG       0U
#define WAV_FMT_OFF_CHANNELS  2U
#define WAV_FMT_OFF_RATE      4U
#define WAV_FMT_OFF_BITS      14U
#define WAV_FMT_OFF_SUBFORMAT 24U

/** Widest sample this library is willing to describe. */
#define WAV_MAX_BITS_PER_SAMPLE 64U

BUILD_ASSERT(WAV_FMT_EXT_SIZE <= WAV_SOURCE_MAX_READ_SIZE,
	     "WAV_SOURCE_MAX_READ_SIZE understates the largest read wav_parse() issues");
BUILD_ASSERT(WAV_MIN_FILE_SIZE >= WAV_CHUNK_HEADER_SIZE,
	     "the chunk walk relies on a source being at least one chunk header long");

/**
 * @brief Read from a source, refusing anything that is not wholly inside it.
 *
 * The bound is expressed as @c len against the space left rather than as @c off + @c len
 * against the size, because that addition is exactly what would wrap on a corrupt container.
 */
static int wav_source_read(const struct wav_source *src, uint32_t off, void *dst, size_t len)
{
	if ((off > src->size) || (len > (size_t)(src->size - off))) {
		return -EINVAL;
	}

	return src->read(src->user_data, off, dst, len);
}

static bool wav_format_is_modelled(uint16_t tag)
{
	switch (tag) {
	case WAV_FORMAT_PCM:
	case WAV_FORMAT_IEEE_FLOAT:
	case WAV_FORMAT_ALAW:
	case WAV_FORMAT_MULAW:
		return true;
	default:
		return false;
	}
}

/**
 * @brief Read the audio format out of the body of a `fmt ` chunk.
 *
 * @param src Byte source holding the container.
 * @param body Offset of the chunk body from the start of the container.
 * @param size Length of the chunk body, already clamped to what @p src holds.
 * @param info Where to store the format. Written even on failure, so the caller must keep it
 *             away from the struct it hands back to the application.
 */
static int wav_parse_fmt(const struct wav_source *src, uint32_t body, uint32_t size,
			 struct wav_info *info)
{
	uint8_t buf[WAV_FMT_EXT_SIZE];
	uint16_t tag;
	int ret;

	if (size < WAV_FMT_MIN_SIZE) {
		LOG_ERR("fmt chunk at +%u is %u B, need at least %u", body, size, WAV_FMT_MIN_SIZE);
		return -EINVAL;
	}

	size = MIN(size, WAV_FMT_EXT_SIZE);

	ret = wav_source_read(src, body, buf, size);
	if (ret < 0) {
		return ret;
	}

	tag = sys_get_le16(&buf[WAV_FMT_OFF_TAG]);

	if (tag == WAV_FORMAT_EXTENSIBLE) {
		/*
		 * The real format tag is the first field of the SubFormat GUID, which only
		 * exists in the 40-byte flavour of the chunk.
		 */
		if (size < WAV_FMT_EXT_SIZE) {
			LOG_ERR("extensible fmt chunk at +%u is %u B, need %u", body, size,
				WAV_FMT_EXT_SIZE);
			return -EINVAL;
		}

		tag = sys_get_le16(&buf[WAV_FMT_OFF_SUBFORMAT]);
	}

	/*
	 * The tag is checked before the numeric fields because it decides how they are to be
	 * read: a compressed format may legitimately carry a sample width this library would
	 * otherwise reject, and reporting that as a malformed file would be misleading.
	 */
	if (!wav_format_is_modelled(tag)) {
		LOG_ERR("format tag 0x%04x is not a flat array of fixed-size frames", tag);
		return -ENOTSUP;
	}

	info->format = (enum wav_format)tag;
	info->num_channels = sys_get_le16(&buf[WAV_FMT_OFF_CHANNELS]);
	info->sample_rate = sys_get_le32(&buf[WAV_FMT_OFF_RATE]);
	info->bits_per_sample = sys_get_le16(&buf[WAV_FMT_OFF_BITS]);

	if ((info->num_channels == 0U) || (info->sample_rate == 0U) ||
	    (info->bits_per_sample == 0U) || ((info->bits_per_sample % 8U) != 0U) ||
	    (info->bits_per_sample > WAV_MAX_BITS_PER_SAMPLE)) {
		LOG_ERR("unusable format: %u channels, %u Hz, %u bits per sample",
			info->num_channels, info->sample_rate, info->bits_per_sample);
		return -EINVAL;
	}

	return 0;
}

int wav_parse(const struct wav_source *src, struct wav_info *info)
{
	uint8_t buf[WAV_MIN_FILE_SIZE];
	struct wav_info found = {0};
	bool have_fmt = false;
	bool have_data = false;
	unsigned int chunk;
	uint32_t off;
	int ret;

	if ((src == NULL) || (src->read == NULL) || (info == NULL)) {
		return -EINVAL;
	}

	if (src->size < WAV_MIN_FILE_SIZE) {
		LOG_ERR("source holds %u B, need at least %u", src->size, WAV_MIN_FILE_SIZE);
		return -EINVAL;
	}

	ret = wav_source_read(src, 0U, buf, WAV_MIN_FILE_SIZE);
	if (ret < 0) {
		return ret;
	}

	if ((memcmp(&buf[0], "RIFF", 4) != 0) || (memcmp(&buf[8], "WAVE", 4) != 0)) {
		LOG_ERR("not a RIFF/WAVE container");
		return -EINVAL;
	}

	off = WAV_MIN_FILE_SIZE;

	for (chunk = 0U; chunk < (unsigned int)CONFIG_WAV_MAX_CHUNKS; chunk++) {
		uint32_t body;
		uint32_t size;

		/*
		 * The pad byte skipped at the end of the previous iteration can leave off one
		 * past the end of the source, so the space left is what gets compared. The
		 * subtraction is safe because the source is at least WAV_MIN_FILE_SIZE long.
		 */
		if (off > (src->size - WAV_CHUNK_HEADER_SIZE)) {
			break;
		}

		ret = wav_source_read(src, off, buf, WAV_CHUNK_HEADER_SIZE);
		if (ret < 0) {
			return ret;
		}

		body = off + WAV_CHUNK_HEADER_SIZE;
		size = sys_get_le32(&buf[4]);

		/*
		 * Trust the container only as far as the source actually goes, and clamp
		 * before the size is used in any arithmetic. A file truncated in transit, or
		 * one still being written and so carrying a placeholder size, stays usable.
		 */
		if (size > (src->size - body)) {
			LOG_WRN("chunk at +%u declares %u B, clamping to %u", off, size,
				src->size - body);
			size = src->size - body;
		}

		if (memcmp(&buf[0], "fmt ", 4) == 0) {
			ret = wav_parse_fmt(src, body, size, &found);
			if (ret < 0) {
				return ret;
			}

			have_fmt = true;
		} else if (memcmp(&buf[0], "data", 4) == 0) {
			found.data_off = body;
			found.data_len = size;
			have_data = true;
		} else {
			LOG_DBG("skipping chunk at +%u (%u B)", off, size);
		}

		if (have_fmt && have_data) {
			/*
			 * Stop as soon as both are in hand: there is nothing left to learn,
			 * and walking the chunks that follow the payload would mean seeking
			 * across the whole file on a slow source.
			 */
			*info = found;
			return 0;
		}

		/*
		 * Chunks are padded to an even length, and the pad byte is not counted in the
		 * size field. This advances by at least WAV_CHUNK_HEADER_SIZE whatever the
		 * size is, zero included, so the walk cannot spin in place.
		 */
		off = body + size + (size & 1U);
	}

	if (chunk == (unsigned int)CONFIG_WAV_MAX_CHUNKS) {
		LOG_ERR("gave up after %u chunks", chunk);
		return -E2BIG;
	}

	LOG_ERR("no '%s' chunk", have_fmt ? "data" : "fmt ");

	return -ENOENT;
}

ssize_t wav_read(const struct wav_source *src, const struct wav_info *info, uint32_t off, void *dst,
		 size_t len)
{
	int ret;

	if ((src == NULL) || (src->read == NULL) || (info == NULL) || (dst == NULL)) {
		return -EINVAL;
	}

	if (off > info->data_len) {
		return -EINVAL;
	}

	len = MIN(len, (size_t)(info->data_len - off));
	if (len == 0U) {
		return 0;
	}

	ret = wav_source_read(src, info->data_off + off, dst, len);
	if (ret < 0) {
		return ret;
	}

	return (ssize_t)len;
}

static int wav_mem_read(void *user_data, uint32_t off, void *dst, size_t len)
{
	memcpy(dst, &((const uint8_t *)user_data)[off], len);

	return 0;
}

int wav_source_mem_init(struct wav_source *src, const void *buf, size_t size)
{
	if ((src == NULL) || (buf == NULL)) {
		return -EINVAL;
	}

#if SIZE_MAX > UINT32_MAX
	if (size > (size_t)UINT32_MAX) {
		return -EINVAL;
	}
#endif

	src->read = wav_mem_read;
	/* Cast away const: wav_mem_read() only ever reads, and the field is documented as
	 * read-only. There is no const-qualified variant of the callback to hang it off.
	 */
	src->user_data = (void *)buf;
	src->size = (uint32_t)size;

	return 0;
}
