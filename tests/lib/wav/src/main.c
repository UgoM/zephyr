/*
 * Copyright (c) 2026 Ugo Marchand
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include <zephyr/audio/wav.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#include "fixtures.h"

/**
 * @brief A byte source that fails the test if the parser reads outside it.
 *
 * wav_parse() is fed untrusted bytes, so the interesting property is not what it returns but
 * where it reads. Every call is checked here, which turns an out-of-bounds read into a test
 * failure instead of something that happens to work on this platform.
 */
struct checked_source {
	const uint8_t *buf;
	uint32_t size;
	/** Number of times the callback has been invoked. */
	unsigned int calls;
	/** Largest read that is legitimate for what is being tested. */
	size_t max_read;
	/** Call number to fail on, counting from one. Zero never fails. */
	unsigned int fail_at;
	/** What to fail with. */
	int fail_ret;
};

static int checked_read(void *user_data, uint32_t off, void *dst, size_t len)
{
	struct checked_source *cs = user_data;

	cs->calls++;

	zassert_true(off <= cs->size, "read at +%u of a %u byte source", off, cs->size);
	zassert_true(len <= (size_t)(cs->size - off), "read of %zu B at +%u runs past %u B", len,
		     off, cs->size);
	zassert_true(len <= cs->max_read, "read of %zu B exceeds the documented maximum %zu", len,
		     cs->max_read);

	if ((cs->fail_at != 0U) && (cs->calls == cs->fail_at)) {
		return cs->fail_ret;
	}

	memcpy(dst, &cs->buf[off], len);

	return 0;
}

static void checked_source_init(struct wav_source *src, struct checked_source *cs,
				const uint8_t *buf, size_t size)
{
	*cs = (struct checked_source){
		.buf = buf,
		.size = (uint32_t)size,
		.max_read = WAV_SOURCE_MAX_READ_SIZE,
	};

	*src = (struct wav_source){
		.read = checked_read,
		.user_data = cs,
		.size = (uint32_t)size,
	};
}

/** Parse through the checked source, which is how every container in here is parsed. */
static int parse_checked(const uint8_t *buf, size_t size, struct wav_info *info)
{
	struct checked_source cs;
	struct wav_source src;
	struct wav_info scratch;

	checked_source_init(&src, &cs, buf, size);

	return wav_parse(&src, (info != NULL) ? info : &scratch);
}

/**
 * @brief Assert the invariants wav_parse() promises on success.
 *
 * Notably that the payload lies inside the source, which is what every caller relies on when
 * it hands @c data_off and @c data_len to a DMA controller.
 */
static void assert_info_sane(const struct wav_info *info, uint32_t size)
{
	zassert_not_equal(info->num_channels, 0, "channel count of zero reported");
	zassert_not_equal(info->sample_rate, 0, "sample rate of zero reported");
	zassert_not_equal(info->bits_per_sample, 0, "sample width of zero reported");
	zassert_equal(info->bits_per_sample % 8U, 0, "sample width %u is not whole bytes",
		      info->bits_per_sample);
	zassert_true(info->bits_per_sample <= 64, "sample width %u reported",
		     info->bits_per_sample);
	zassert_not_equal(info->format, WAV_FORMAT_EXTENSIBLE, "extensible left unresolved");
	zassert_true(info->data_off <= size, "payload starts past the source");
	zassert_true(info->data_len <= (size - info->data_off), "payload runs past the source");
	zassert_not_equal(wav_frame_size(info), 0, "frame size of zero reported");
}

ZTEST_SUITE(wav_valid, NULL, NULL, NULL, NULL, NULL);

ZTEST(wav_valid, test_canonical)
{
	struct wav_info info;

	zassert_ok(parse_checked(wav_canonical, sizeof(wav_canonical), &info));
	zassert_equal(info.format, WAV_FORMAT_PCM);
	zassert_equal(info.num_channels, 1);
	zassert_equal(info.sample_rate, 16000);
	zassert_equal(info.bits_per_sample, 16);
	zassert_equal(info.data_off, 44, "the textbook layout does put the payload at 44");
	zassert_equal(info.data_len, 8);
}

ZTEST(wav_valid, test_ffmpeg_metadata_chunk)
{
	struct wav_info info;

	zassert_ok(parse_checked(wav_ffmpeg, sizeof(wav_ffmpeg), &info));
	zassert_equal(info.data_off, 108, "a LIST chunk pushes the payload past offset 44");
	zassert_equal(info.data_len, 8);
	zassert_equal(info.sample_rate, 16000);
}

ZTEST(wav_valid, test_stereo)
{
	struct wav_info info;

	zassert_ok(parse_checked(wav_stereo, sizeof(wav_stereo), &info));
	zassert_equal(info.num_channels, 2);
	zassert_equal(info.sample_rate, 22050);
	zassert_equal(wav_frame_size(&info), 4);
	zassert_equal(wav_frame_count(&info), 2);
}

ZTEST(wav_valid, test_odd_length_chunk_is_padded)
{
	struct wav_info info;

	zassert_ok(parse_checked(wav_odd_pad, sizeof(wav_odd_pad), &info));
	zassert_equal(info.data_off, 56, "the pad byte after an odd chunk was not skipped");
	zassert_equal(info.data_len, 8);
}

ZTEST(wav_valid, test_fmt_chunk_of_18_bytes)
{
	struct wav_info info;

	zassert_ok(parse_checked(wav_fmt18, sizeof(wav_fmt18), &info));
	zassert_equal(info.sample_rate, 44100);
	zassert_equal(info.data_off, 46);
}

ZTEST(wav_valid, test_extensible_resolves_to_pcm)
{
	struct wav_info info;

	zassert_ok(parse_checked(wav_extensible, sizeof(wav_extensible), &info));
	zassert_equal(info.format, WAV_FORMAT_PCM, "the SubFormat GUID was not read");
	zassert_equal(info.num_channels, 6);
	zassert_equal(info.bits_per_sample, 24);
	zassert_equal(wav_frame_size(&info), 18);
	zassert_equal(wav_frame_count(&info), 2);
}

ZTEST(wav_valid, test_uncompressed_formats)
{
	struct wav_info info;

	zassert_ok(parse_checked(wav_float32, sizeof(wav_float32), &info));
	zassert_equal(info.format, WAV_FORMAT_IEEE_FLOAT);
	zassert_equal(info.bits_per_sample, 32);

	zassert_ok(parse_checked(wav_mulaw, sizeof(wav_mulaw), &info));
	zassert_equal(info.format, WAV_FORMAT_MULAW);

	zassert_ok(parse_checked(wav_alaw, sizeof(wav_alaw), &info));
	zassert_equal(info.format, WAV_FORMAT_ALAW);
}

ZTEST(wav_valid, test_data_before_fmt)
{
	struct wav_info info;

	zassert_ok(parse_checked(wav_data_first, sizeof(wav_data_first), &info));
	zassert_equal(info.data_off, 20);
	zassert_equal(info.sample_rate, 16000);
}

ZTEST(wav_valid, test_empty_payload)
{
	struct wav_info info;

	zassert_ok(parse_checked(wav_empty_data, sizeof(wav_empty_data), &info),
		   "a header with no audio behind it is still a valid header");
	zassert_equal(info.data_len, 0);
	zassert_equal(wav_frame_count(&info), 0);
	zassert_equal(wav_duration_ms(&info), 0);
}

ZTEST(wav_valid, test_unbounded_payload_is_clamped)
{
	struct wav_info info;

	zassert_ok(parse_checked(wav_data_unbounded, sizeof(wav_data_unbounded), &info));
	zassert_equal(info.data_len, 8, "a payload size of 0xFFFFFFFF was not clamped");
	zassert_equal(info.data_off + info.data_len, sizeof(wav_data_unbounded));
}

ZTEST(wav_valid, test_walk_stops_at_the_payload)
{
	struct checked_source cs;
	struct wav_source src;
	struct wav_info info;

	checked_source_init(&src, &cs, wav_trailing, sizeof(wav_trailing));

	zassert_ok(wav_parse(&src, &info));
	/* Preamble, then one chunk header each for `fmt ` and `data`, plus the `fmt ` body:
	 * four reads. Anything more means the chunks behind the payload were walked too.
	 */
	zassert_equal(cs.calls, 4, "%u reads, so the walk did not stop at the payload", cs.calls);
}

ZTEST(wav_valid, test_no_output_written_on_failure)
{
	struct wav_info info;

	memset(&info, 0xA5, sizeof(info));

	zassert_equal(parse_checked(wav_adpcm, sizeof(wav_adpcm), &info), -ENOTSUP);

	for (size_t i = 0; i < sizeof(info); i++) {
		zassert_equal(((const uint8_t *)&info)[i], 0xA5,
			      "a failed parse wrote to the caller's struct");
	}
}

ZTEST_SUITE(wav_malformed, NULL, NULL, NULL, NULL, NULL);

ZTEST(wav_malformed, test_too_short)
{
	/* Every prefix of a real container that is shorter than the preamble. */
	for (size_t size = 0; size < WAV_MIN_FILE_SIZE; size++) {
		zassert_equal(parse_checked(wav_canonical, size, NULL), -EINVAL,
			      "a %zu byte source was accepted", size);
	}
}

ZTEST(wav_malformed, test_wrong_container)
{
	zassert_equal(parse_checked(wav_riffx, sizeof(wav_riffx), NULL), -EINVAL,
		      "a big-endian RIFX container was accepted");
	zassert_equal(parse_checked(wav_avi, sizeof(wav_avi), NULL), -EINVAL,
		      "a RIFF container holding AVI was accepted");
}

ZTEST(wav_malformed, test_missing_chunks)
{
	zassert_equal(parse_checked(wav_preamble_only, sizeof(wav_preamble_only), NULL), -ENOENT);
	zassert_equal(parse_checked(wav_no_data, sizeof(wav_no_data), NULL), -ENOENT);
	zassert_equal(parse_checked(wav_no_fmt, sizeof(wav_no_fmt), NULL), -ENOENT);
}

ZTEST(wav_malformed, test_truncated_fmt)
{
	zassert_equal(parse_checked(wav_fmt_truncated, sizeof(wav_fmt_truncated), NULL), -EINVAL);
	zassert_equal(parse_checked(wav_extensible_no_guid, sizeof(wav_extensible_no_guid), NULL),
		      -EINVAL, "extensible without its SubFormat GUID was accepted");
}

ZTEST(wav_malformed, test_chunk_size_is_clamped_before_use)
{
	zassert_equal(parse_checked(wav_chunk_unbounded, sizeof(wav_chunk_unbounded), NULL),
		      -ENOENT);
	zassert_equal(parse_checked(wav_size_wraps, sizeof(wav_size_wraps), NULL), -ENOENT,
		      "a chunk size chosen to wrap the offset was not clamped first");
}

ZTEST(wav_malformed, test_unusable_format_fields)
{
	zassert_equal(parse_checked(wav_zero_channels, sizeof(wav_zero_channels), NULL), -EINVAL);
	zassert_equal(parse_checked(wav_zero_rate, sizeof(wav_zero_rate), NULL), -EINVAL);
	zassert_equal(parse_checked(wav_zero_bits, sizeof(wav_zero_bits), NULL), -EINVAL);
	zassert_equal(parse_checked(wav_bits12, sizeof(wav_bits12), NULL), -EINVAL,
		      "12 bits per sample is not a whole number of bytes");
	zassert_equal(parse_checked(wav_bits128, sizeof(wav_bits128), NULL), -EINVAL);
}

ZTEST(wav_malformed, test_formats_that_are_not_flat_frames)
{
	zassert_equal(parse_checked(wav_adpcm, sizeof(wav_adpcm), NULL), -ENOTSUP);
	zassert_equal(parse_checked(wav_mp3, sizeof(wav_mp3), NULL), -ENOTSUP);
	zassert_equal(parse_checked(wav_extensible_adpcm, sizeof(wav_extensible_adpcm), NULL),
		      -ENOTSUP, "a SubFormat GUID naming ADPCM was accepted");
}

ZTEST(wav_malformed, test_null_arguments)
{
	struct checked_source cs;
	struct wav_source src;
	struct wav_info info;

	checked_source_init(&src, &cs, wav_canonical, sizeof(wav_canonical));

	zassert_equal(wav_parse(NULL, &info), -EINVAL);
	zassert_equal(wav_parse(&src, NULL), -EINVAL);

	src.read = NULL;
	zassert_equal(wav_parse(&src, &info), -EINVAL, "a source with no read callback was used");
}

ZTEST(wav_malformed, test_source_errors_are_propagated)
{
	/* Fail on each read in turn. Whichever one it is, the errno has to come back
	 * untouched, and the parser must not have read anywhere it should not.
	 */
	for (unsigned int nth = 1; nth <= 8U; nth++) {
		struct checked_source cs;
		struct wav_source src;
		struct wav_info info;
		int ret;

		checked_source_init(&src, &cs, wav_ffmpeg, sizeof(wav_ffmpeg));
		cs.fail_at = nth;
		cs.fail_ret = -EIO;

		ret = wav_parse(&src, &info);

		if (cs.calls < nth) {
			/* The parse finished before reaching that read. */
			zassert_ok(ret);
			continue;
		}

		zassert_equal(ret, -EIO, "read %u failed with -EIO but wav_parse() returned %d",
			      nth, ret);
	}
}

ZTEST_SUITE(wav_limits, NULL, NULL, NULL, NULL, NULL);

/** Append @p count chunks that hold nothing and mean nothing. */
static size_t append_filler(uint8_t *buf, size_t off, unsigned int count)
{
	for (unsigned int i = 0; i < count; i++) {
		memcpy(&buf[off], "junk", 4);
		sys_put_le32(0U, &buf[off + 4]);
		off += 8U;
	}

	return off;
}

/* Preamble, the fillers, a 16-byte `fmt ` chunk and an empty `data` chunk. */
#define FILLER_BUF_SIZE (WAV_MIN_FILE_SIZE + ((CONFIG_WAV_MAX_CHUNKS + 1) * 8U) + 24U + 8U)

static size_t build_with_filler(uint8_t *buf, unsigned int fillers)
{
	static const uint8_t tail[] = {
		FMT_PCM(1, 16000, 16),
		CHUNK_HEADER('d', 'a', 't', 'a', 0),
	};
	static const uint8_t preamble[] = {RIFF_WAVE(4)};
	size_t off;

	memcpy(buf, preamble, sizeof(preamble));
	off = append_filler(buf, sizeof(preamble), fillers);
	memcpy(&buf[off], tail, sizeof(tail));

	return off + sizeof(tail);
}

ZTEST(wav_limits, test_chunk_list_within_the_limit)
{
	static uint8_t buf[FILLER_BUF_SIZE];
	struct wav_info info;
	size_t size;

	/* Enough fillers that the format and the payload land on the last two allowed
	 * iterations of the walk.
	 */
	size = build_with_filler(buf, CONFIG_WAV_MAX_CHUNKS - 2);

	zassert_ok(parse_checked(buf, size, &info),
		   "a chunk list of exactly the limit was refused");
	zassert_equal(info.sample_rate, 16000);
	zassert_equal(info.data_len, 0);
}

ZTEST(wav_limits, test_chunk_list_over_the_limit)
{
	static uint8_t buf[FILLER_BUF_SIZE];
	size_t size = build_with_filler(buf, CONFIG_WAV_MAX_CHUNKS);

	zassert_equal(parse_checked(buf, size, NULL), -E2BIG,
		      "a chunk list longer than CONFIG_WAV_MAX_CHUNKS did not bound the walk");
}

ZTEST_SUITE(wav_payload, NULL, NULL, NULL, NULL, NULL);

ZTEST(wav_payload, test_read_whole_payload)
{
	struct checked_source cs;
	struct wav_source src;
	struct wav_info info;
	uint8_t dst[16];

	checked_source_init(&src, &cs, wav_ffmpeg, sizeof(wav_ffmpeg));
	zassert_ok(wav_parse(&src, &info));

	cs.max_read = sizeof(dst);

	zassert_equal(wav_read(&src, &info, 0U, dst, sizeof(dst)), 8,
		      "a read longer than the payload was not clamped to it");
	zassert_mem_equal(dst, &wav_ffmpeg[info.data_off], 8, "wrong bytes returned");
}

ZTEST(wav_payload, test_read_in_blocks)
{
	struct checked_source cs;
	struct wav_source src;
	struct wav_info info;
	uint8_t dst[36];
	uint32_t off = 0;

	checked_source_init(&src, &cs, wav_extensible, sizeof(wav_extensible));
	zassert_ok(wav_parse(&src, &info));

	/* Refill in whole frames, the way a playback loop does. */
	while (off < info.data_len) {
		ssize_t ret = wav_read(&src, &info, off, &dst[off], wav_frame_size(&info));

		zassert_true(ret > 0, "wav_read() returned %zd part way through", ret);
		off += (uint32_t)ret;
	}

	zassert_equal(off, info.data_len);
	zassert_mem_equal(dst, &wav_extensible[info.data_off], info.data_len);

	zassert_equal(wav_read(&src, &info, off, dst, sizeof(dst)), 0,
		      "the end of the payload is not an error");
}

ZTEST(wav_payload, test_read_edges)
{
	struct checked_source cs;
	struct wav_source src;
	struct wav_info info;
	uint8_t dst[8];

	checked_source_init(&src, &cs, wav_canonical, sizeof(wav_canonical));
	zassert_ok(wav_parse(&src, &info));

	zassert_equal(wav_read(&src, &info, 0U, dst, 0U), 0, "a zero length read is not an error");
	zassert_equal(wav_read(&src, &info, info.data_len, dst, sizeof(dst)), 0);
	zassert_equal(wav_read(&src, &info, info.data_len + 1U, dst, sizeof(dst)), -EINVAL,
		      "a read starting past the payload was accepted");
	zassert_equal(wav_read(&src, &info, 0U, NULL, sizeof(dst)), -EINVAL);
	zassert_equal(wav_read(NULL, &info, 0U, dst, sizeof(dst)), -EINVAL);
	zassert_equal(wav_read(&src, NULL, 0U, dst, sizeof(dst)), -EINVAL);
}

ZTEST(wav_payload, test_read_errors_are_propagated)
{
	struct checked_source cs;
	struct wav_source src;
	struct wav_info info;
	uint8_t dst[8];

	checked_source_init(&src, &cs, wav_canonical, sizeof(wav_canonical));
	zassert_ok(wav_parse(&src, &info));

	cs.fail_at = cs.calls + 1U;
	cs.fail_ret = -ENOSPC;

	zassert_equal(wav_read(&src, &info, 0U, dst, sizeof(dst)), -ENOSPC,
		      "the source's errno was not passed through");
}

ZTEST_SUITE(wav_helpers, NULL, NULL, NULL, NULL, NULL);

ZTEST(wav_helpers, test_frame_arithmetic)
{
	struct wav_info info = {
		.sample_rate = 48000,
		.num_channels = 6,
		.bits_per_sample = 24,
		.data_len = 18U * 100U,
	};

	zassert_equal(wav_frame_size(&info), 18);
	zassert_equal(wav_frame_count(&info), 100);

	/* A partial trailing frame is not a frame. */
	info.data_len += 7U;
	zassert_equal(wav_frame_count(&info), 100);
}

ZTEST(wav_helpers, test_duration)
{
	struct wav_info info = {
		.sample_rate = 16000,
		.num_channels = 1,
		.bits_per_sample = 16,
		.data_len = 16000U * 2U * 5U,
	};

	zassert_equal(wav_duration_ms(&info), 5000);

	/* Rounded down, not up. */
	info.data_len = 31U * 2U;
	zassert_equal(wav_duration_ms(&info), 1);

	/*
	 * Just over four and a half minutes at this rate is where multiplying the frame count
	 * by a thousand stops fitting in 32 bits. Getting this wrong reports a long recording
	 * as a few milliseconds.
	 */
	info.data_len = 8000000U * 2U;
	zassert_equal(wav_duration_ms(&info), 500000,
		      "the frame count times a thousand overflowed");

	/* The largest payload a 32-bit size can describe, one channel of 8-bit at 8 kHz:
	 * around six days, which still fits in milliseconds.
	 */
	info.sample_rate = 8000;
	info.bits_per_sample = 8;
	info.data_len = UINT32_MAX;
	zassert_equal(wav_duration_ms(&info), 536870911);
}

ZTEST_SUITE(wav_sweep, NULL, NULL, NULL, NULL, NULL);

/** The codes wav_parse() is documented to return when the source itself never fails. */
static void assert_documented(int ret, const char *what)
{
	switch (ret) {
	case 0:
	case -EINVAL:
	case -ENOENT:
	case -ENOTSUP:
	case -E2BIG:
		break;
	default:
		zassert_unreachable("%s: undocumented return %d", what, ret);
	}
}

ZTEST(wav_sweep, test_every_fixture_returns_what_it_should)
{
	ARRAY_FOR_EACH_PTR(wav_fixtures, f) {
		struct wav_info info;
		int ret = parse_checked(f->buf, f->size, &info);

		zassert_equal(ret, f->expected, "%s: expected %d, got %d", f->name, f->expected,
			      ret);

		if (ret == 0) {
			assert_info_sane(&info, (uint32_t)f->size);
		}
	}
}

ZTEST(wav_sweep, test_truncation_at_every_length)
{
	/*
	 * Truncating a container is the failure mode a partial download or a full flash
	 * produces, and every prefix has to come back with a documented error rather than a
	 * read past the end. The checked source is what actually enforces the second half.
	 */
	ARRAY_FOR_EACH_PTR(wav_fixtures, f) {
		for (size_t size = 0; size <= f->size; size++) {
			struct wav_info info;
			int ret = parse_checked(f->buf, size, &info);

			assert_documented(ret, f->name);

			if (ret == 0) {
				assert_info_sane(&info, (uint32_t)size);
			}
		}
	}
}

ZTEST(wav_sweep, test_single_byte_mutations)
{
	/*
	 * A cheap stand-in for a fuzzer: poke each byte in turn with the values that tend to
	 * break length arithmetic, and require only that the parser comes back with something
	 * it documents and never reads outside the buffer.
	 */
	static const uint8_t pokes[] = {0x00, 0x01, 0x7F, 0xFF};
	static uint8_t buf[sizeof(wav_ffmpeg)];

	ARRAY_FOR_EACH(pokes, p) {
		for (size_t i = 0; i < sizeof(buf); i++) {
			struct wav_info info;
			int ret;

			memcpy(buf, wav_ffmpeg, sizeof(buf));
			buf[i] = pokes[p];

			ret = parse_checked(buf, sizeof(buf), &info);
			assert_documented(ret, "mutated wav_ffmpeg");

			if (ret == 0) {
				assert_info_sane(&info, (uint32_t)sizeof(buf));
			}
		}
	}
}
