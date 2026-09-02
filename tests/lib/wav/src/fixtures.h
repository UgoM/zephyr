/*
 * Copyright (c) 2026 Ugo Marchand
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Hand-built RIFF/WAVE containers.
 *
 * They are byte arrays rather than real .wav files because the compliance checks refuse
 * binary files in tree, and because a hostile container cannot be produced by an encoder in
 * the first place.
 */

#ifndef ZEPHYR_TESTS_LIB_WAV_FIXTURES_H_
#define ZEPHYR_TESTS_LIB_WAV_FIXTURES_H_

#include <errno.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/audio/wav.h>

/* clang-format off */

#define LE16(v) (uint8_t)((v) & 0xFFU), (uint8_t)(((v) >> 8) & 0xFFU)

#define LE32(v)								\
	(uint8_t)((v) & 0xFFU), (uint8_t)(((v) >> 8) & 0xFFU),		\
	(uint8_t)(((v) >> 16) & 0xFFU), (uint8_t)(((v) >> 24) & 0xFFU)

#define FOURCC(a, b, c, d) (uint8_t)(a), (uint8_t)(b), (uint8_t)(c), (uint8_t)(d)

#define CHUNK_HEADER(a, b, c, d, size) FOURCC(a, b, c, d), LE32(size)

#define RIFF_WAVE(size) CHUNK_HEADER('R', 'I', 'F', 'F', size), FOURCC('W', 'A', 'V', 'E')

/** A 16-byte `fmt ` chunk, the flavour every encoder writes for integer PCM. */
#define FMT_PCM(ch, rate, bits)                                                                    \
	CHUNK_HEADER('f', 'm', 't', ' ', 16),                                                      \
	LE16(WAV_FORMAT_PCM), LE16(ch), LE32(rate),                                                \
	LE32((rate) * (ch) * ((bits) / 8)), LE16((ch) * ((bits) / 8)), LE16(bits)

/** As FMT_PCM(), with the format tag left free so a rejection can be provoked. */
#define FMT_TAG(tag, ch, rate, bits)                                                               \
	CHUNK_HEADER('f', 'm', 't', ' ', 16),                                                      \
	LE16(tag), LE16(ch), LE32(rate),                                                           \
	LE32((rate) * (ch) * ((bits) / 8)), LE16((ch) * ((bits) / 8)), LE16(bits)

/** KSDATAFORMAT_SUBTYPE_* GUID, whose first field is the effective format tag. */
#define SUBFORMAT_GUID(tag)                                                                        \
	LE32(tag), LE16(0x0000), LE16(0x0010),                                                     \
	0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71

/** The 40-byte `WAVE_FORMAT_EXTENSIBLE` flavour, used by most >16-bit and >2-channel files. */
#define FMT_EXTENSIBLE(sub, ch, rate, bits)                                                        \
	CHUNK_HEADER('f', 'm', 't', ' ', 40),                                                      \
	LE16(WAV_FORMAT_EXTENSIBLE), LE16(ch), LE32(rate),                                         \
	LE32((rate) * (ch) * ((bits) / 8)), LE16((ch) * ((bits) / 8)), LE16(bits),                 \
	LE16(22), LE16(bits), LE32(0x0000003F), SUBFORMAT_GUID(sub)

#define EIGHT_BYTES 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77

/* Well-formed containers. Each one also has to survive being read back from a file. */

/** The textbook layout, and the only one where the payload really does start at offset 44. */
static const uint8_t wav_canonical[] = {
	RIFF_WAVE(44),
	FMT_PCM(1, 16000, 16),
	CHUNK_HEADER('d', 'a', 't', 'a', 8),
	EIGHT_BYTES,
};

/**
 * What ffmpeg actually writes: a LIST/INFO chunk sits between the format and the payload, so
 * the payload starts at offset 108. Assuming 44 is the mistake this library exists to stop.
 */
static const uint8_t wav_ffmpeg[] = {
	RIFF_WAVE(108),
	FMT_PCM(1, 16000, 16),
	CHUNK_HEADER('L', 'I', 'S', 'T', 56),
	FOURCC('I', 'N', 'F', 'O'),
	CHUNK_HEADER('I', 'S', 'F', 'T', 44),
	'L', 'a', 'v', 'f', '6', '1', '.', '7', '.', '1', '0', '0',
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	CHUNK_HEADER('d', 'a', 't', 'a', 8),
	EIGHT_BYTES,
};

static const uint8_t wav_stereo[] = {
	RIFF_WAVE(44),
	FMT_PCM(2, 22050, 16),
	CHUNK_HEADER('d', 'a', 't', 'a', 8),
	EIGHT_BYTES,
};

/** A chunk of odd length, so the walk has to step over the pad byte that follows it. */
static const uint8_t wav_odd_pad[] = {
	RIFF_WAVE(44),
	FMT_PCM(1, 8000, 8),
	CHUNK_HEADER('j', 'u', 'n', 'k', 3),
	0xAA, 0xBB, 0xCC, 0x00,
	CHUNK_HEADER('d', 'a', 't', 'a', 8),
	EIGHT_BYTES,
};

/** An 18-byte `fmt ` chunk: legal, and two bytes longer than the one everybody expects. */
static const uint8_t wav_fmt18[] = {
	RIFF_WAVE(46),
	CHUNK_HEADER('f', 'm', 't', ' ', 18),
	LE16(WAV_FORMAT_PCM), LE16(1), LE32(44100), LE32(88200), LE16(2), LE16(16),
	LE16(0),
	CHUNK_HEADER('d', 'a', 't', 'a', 8),
	EIGHT_BYTES,
};

/** 24-bit six-channel, which is only expressible through a SubFormat GUID. */
static const uint8_t wav_extensible[] = {
	RIFF_WAVE(68),
	FMT_EXTENSIBLE(WAV_FORMAT_PCM, 6, 48000, 24),
	CHUNK_HEADER('d', 'a', 't', 'a', 36),
	EIGHT_BYTES, EIGHT_BYTES, EIGHT_BYTES, EIGHT_BYTES, 0x88, 0x99, 0xAA, 0xBB,
};

static const uint8_t wav_float32[] = {
	RIFF_WAVE(44),
	FMT_TAG(WAV_FORMAT_IEEE_FLOAT, 2, 48000, 32),
	CHUNK_HEADER('d', 'a', 't', 'a', 8),
	EIGHT_BYTES,
};

static const uint8_t wav_mulaw[] = {
	RIFF_WAVE(44),
	FMT_TAG(WAV_FORMAT_MULAW, 1, 8000, 8),
	CHUNK_HEADER('d', 'a', 't', 'a', 8),
	EIGHT_BYTES,
};

static const uint8_t wav_alaw[] = {
	RIFF_WAVE(44),
	FMT_TAG(WAV_FORMAT_ALAW, 1, 8000, 8),
	CHUNK_HEADER('d', 'a', 't', 'a', 8),
	EIGHT_BYTES,
};

/** Nothing says the format has to come first. */
static const uint8_t wav_data_first[] = {
	RIFF_WAVE(44),
	CHUNK_HEADER('d', 'a', 't', 'a', 8),
	EIGHT_BYTES,
	FMT_PCM(1, 16000, 16),
};

/** Chunks after the payload, which the walk must never reach. */
static const uint8_t wav_trailing[] = {
	RIFF_WAVE(64),
	FMT_PCM(1, 16000, 16),
	CHUNK_HEADER('d', 'a', 't', 'a', 8),
	EIGHT_BYTES,
	CHUNK_HEADER('L', 'I', 'S', 'T', 4),
	FOURCC('I', 'N', 'F', 'O'),
};

/** A header with no audio behind it: valid, and worth one millisecond of nothing. */
static const uint8_t wav_empty_data[] = {
	RIFF_WAVE(36),
	FMT_PCM(1, 16000, 16),
	CHUNK_HEADER('d', 'a', 't', 'a', 0),
};

/**
 * A payload size of 0xFFFFFFFF, as written by anything streaming to a pipe that cannot seek
 * back to patch the header. Has to be clamped, not refused.
 */
static const uint8_t wav_data_unbounded[] = {
	RIFF_WAVE(0xFFFFFFFF),
	FMT_PCM(1, 16000, 16),
	CHUNK_HEADER('d', 'a', 't', 'a', 0xFFFFFFFF),
	EIGHT_BYTES,
};

/* Containers that must be refused. Every one of these is also fed to the byte source that
 * asserts on out-of-range reads, and to the differential sweep.
 */

static const uint8_t wav_riffx[] = {
	CHUNK_HEADER('R', 'I', 'F', 'X', 44),
	FOURCC('W', 'A', 'V', 'E'),
	FMT_PCM(1, 16000, 16),
	CHUNK_HEADER('d', 'a', 't', 'a', 8),
	EIGHT_BYTES,
};

static const uint8_t wav_avi[] = {
	CHUNK_HEADER('R', 'I', 'F', 'F', 44),
	FOURCC('A', 'V', 'I', ' '),
	FMT_PCM(1, 16000, 16),
	CHUNK_HEADER('d', 'a', 't', 'a', 8),
	EIGHT_BYTES,
};

/** Exactly the preamble, and not one chunk. */
static const uint8_t wav_preamble_only[] = {
	RIFF_WAVE(4),
};

static const uint8_t wav_no_data[] = {
	RIFF_WAVE(36),
	FMT_PCM(1, 16000, 16),
};

static const uint8_t wav_no_fmt[] = {
	RIFF_WAVE(28),
	CHUNK_HEADER('L', 'I', 'S', 'T', 4),
	FOURCC('I', 'N', 'F', 'O'),
	CHUNK_HEADER('d', 'a', 't', 'a', 8),
	EIGHT_BYTES,
};

/** A `fmt ` chunk declaring sixteen bytes with eight left in the container. */
static const uint8_t wav_fmt_truncated[] = {
	RIFF_WAVE(24),
	CHUNK_HEADER('f', 'm', 't', ' ', 16),
	LE16(WAV_FORMAT_PCM), LE16(1), LE32(16000),
};

/** A non-payload chunk claiming the whole address space: clamped, and then nothing follows. */
static const uint8_t wav_chunk_unbounded[] = {
	RIFF_WAVE(0xFFFFFFFF),
	CHUNK_HEADER('L', 'I', 'S', 'T', 0xFFFFFFFF),
	FMT_PCM(1, 16000, 16),
	CHUNK_HEADER('d', 'a', 't', 'a', 8),
	EIGHT_BYTES,
};

/**
 * A chunk size picked so that adding it to the body offset wraps a 32-bit unsigned. Clamping
 * before that addition is the whole reason this parses to a plain error instead of reading
 * from a small offset it was never pointed at.
 */
static const uint8_t wav_size_wraps[] = {
	RIFF_WAVE(44),
	CHUNK_HEADER('L', 'I', 'S', 'T', 0xFFFFFFF8),
	FMT_PCM(1, 16000, 16),
	CHUNK_HEADER('d', 'a', 't', 'a', 8),
	EIGHT_BYTES,
};

static const uint8_t wav_zero_channels[] = {
	RIFF_WAVE(44),
	CHUNK_HEADER('f', 'm', 't', ' ', 16),
	LE16(WAV_FORMAT_PCM), LE16(0), LE32(16000), LE32(32000), LE16(2), LE16(16),
	CHUNK_HEADER('d', 'a', 't', 'a', 8),
	EIGHT_BYTES,
};

static const uint8_t wav_zero_rate[] = {
	RIFF_WAVE(44),
	FMT_PCM(1, 0, 16),
	CHUNK_HEADER('d', 'a', 't', 'a', 8),
	EIGHT_BYTES,
};

static const uint8_t wav_zero_bits[] = {
	RIFF_WAVE(44),
	CHUNK_HEADER('f', 'm', 't', ' ', 16),
	LE16(WAV_FORMAT_PCM), LE16(1), LE32(16000), LE32(32000), LE16(2), LE16(0),
	CHUNK_HEADER('d', 'a', 't', 'a', 8),
	EIGHT_BYTES,
};

/** Twelve bits per sample: a real thing in the wild, and not a whole number of bytes. */
static const uint8_t wav_bits12[] = {
	RIFF_WAVE(44),
	CHUNK_HEADER('f', 'm', 't', ' ', 16),
	LE16(WAV_FORMAT_PCM), LE16(1), LE32(16000), LE32(24000), LE16(2), LE16(12),
	CHUNK_HEADER('d', 'a', 't', 'a', 8),
	EIGHT_BYTES,
};

static const uint8_t wav_bits128[] = {
	RIFF_WAVE(44),
	CHUNK_HEADER('f', 'm', 't', ' ', 16),
	LE16(WAV_FORMAT_PCM), LE16(1), LE32(16000), LE32(256000), LE16(16), LE16(128),
	CHUNK_HEADER('d', 'a', 't', 'a', 8),
	EIGHT_BYTES,
};

/** IMA ADPCM: a valid format, but its payload is not an array of fixed-size frames. */
static const uint8_t wav_adpcm[] = {
	RIFF_WAVE(44),
	CHUNK_HEADER('f', 'm', 't', ' ', 16),
	LE16(0x0011), LE16(1), LE32(16000), LE32(8000), LE16(256), LE16(4),
	CHUNK_HEADER('d', 'a', 't', 'a', 8),
	EIGHT_BYTES,
};

/** MP3 wrapped in a RIFF container. */
static const uint8_t wav_mp3[] = {
	RIFF_WAVE(44),
	FMT_TAG(0x0055, 2, 44100, 16),
	CHUNK_HEADER('d', 'a', 't', 'a', 8),
	EIGHT_BYTES,
};

/** `WAVE_FORMAT_EXTENSIBLE` in a 16-byte chunk, so the GUID that defines it is missing. */
static const uint8_t wav_extensible_no_guid[] = {
	RIFF_WAVE(44),
	FMT_TAG(WAV_FORMAT_EXTENSIBLE, 2, 48000, 16),
	CHUNK_HEADER('d', 'a', 't', 'a', 8),
	EIGHT_BYTES,
};

/** A GUID resolving to a format that is not a flat array of frames. */
static const uint8_t wav_extensible_adpcm[] = {
	RIFF_WAVE(68),
	FMT_EXTENSIBLE(0x0011, 2, 16000, 16),
	CHUNK_HEADER('d', 'a', 't', 'a', 8),
	EIGHT_BYTES,
};

/* clang-format on */

struct wav_fixture {
	const char *name;
	const uint8_t *buf;
	size_t size;
	/** What wav_parse() must return for it. */
	int expected;
};

#define FIXTURE(sym, ret)                                                                          \
	{                                                                                          \
		.name = #sym,                                                                      \
		.buf = sym,                                                                        \
		.size = sizeof(sym),                                                               \
		.expected = (ret),                                                                 \
	}

/**
 * Every container above, and what it parses to.
 *
 * Driven over by the differential sweep and by the file system equivalence suite, so that
 * both of those grow automatically whenever a container is added here.
 */
static const struct wav_fixture wav_fixtures[] = {
	FIXTURE(wav_canonical, 0),
	FIXTURE(wav_ffmpeg, 0),
	FIXTURE(wav_stereo, 0),
	FIXTURE(wav_odd_pad, 0),
	FIXTURE(wav_fmt18, 0),
	FIXTURE(wav_extensible, 0),
	FIXTURE(wav_float32, 0),
	FIXTURE(wav_mulaw, 0),
	FIXTURE(wav_alaw, 0),
	FIXTURE(wav_data_first, 0),
	FIXTURE(wav_trailing, 0),
	FIXTURE(wav_empty_data, 0),
	FIXTURE(wav_data_unbounded, 0),
	FIXTURE(wav_riffx, -EINVAL),
	FIXTURE(wav_avi, -EINVAL),
	FIXTURE(wav_preamble_only, -ENOENT),
	FIXTURE(wav_no_data, -ENOENT),
	FIXTURE(wav_no_fmt, -ENOENT),
	FIXTURE(wav_fmt_truncated, -EINVAL),
	FIXTURE(wav_chunk_unbounded, -ENOENT),
	FIXTURE(wav_size_wraps, -ENOENT),
	FIXTURE(wav_zero_channels, -EINVAL),
	FIXTURE(wav_zero_rate, -EINVAL),
	FIXTURE(wav_zero_bits, -EINVAL),
	FIXTURE(wav_bits12, -EINVAL),
	FIXTURE(wav_bits128, -EINVAL),
	FIXTURE(wav_adpcm, -ENOTSUP),
	FIXTURE(wav_mp3, -ENOTSUP),
	FIXTURE(wav_extensible_no_guid, -EINVAL),
	FIXTURE(wav_extensible_adpcm, -ENOTSUP),
};

#endif /* ZEPHYR_TESTS_LIB_WAV_FIXTURES_H_ */
