/*
 * Copyright (c) 2026 Ugo Marchand
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The point of putting a callback between the parser and the bytes is that a container in
 * flash and a container in a file take the same code path. This asserts exactly that: every
 * fixture, well formed and hostile alike, is written to a file and has to parse to the same
 * result and hand back the same payload as it does from memory.
 */

#include <zephyr/kernel.h>

#ifdef CONFIG_WAV_FS

#include <errno.h>
#include <stdint.h>

#include <zephyr/audio/wav.h>
#include <zephyr/fs/fs.h>
#include <zephyr/fs/littlefs.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#include "fixtures.h"

FS_LITTLEFS_DECLARE_DEFAULT_CONFIG(storage);

#define MOUNT_POINT "/lfs"
#define CLIP_PATH   MOUNT_POINT "/clip.wav"

static struct fs_mount_t mnt = {
	.type = FS_LITTLEFS,
	.fs_data = &storage,
	.storage_dev = (void *)PARTITION_ID(storage_partition),
	.mnt_point = MOUNT_POINT,
};

static void *wav_fs_setup(void)
{
	zassert_ok(fs_mkfs(FS_LITTLEFS, (uintptr_t)mnt.storage_dev, &storage, 0),
		   "could not format the test partition");
	zassert_ok(fs_mount(&mnt), "could not mount the test partition");

	return NULL;
}

static void wav_fs_teardown(void *unused)
{
	ARG_UNUSED(unused);

	(void)fs_unmount(&mnt);
}

static void write_clip(const uint8_t *buf, size_t size, const char *name)
{
	struct fs_file_t file;
	ssize_t written;

	fs_file_t_init(&file);

	zassert_ok(fs_open(&file, CLIP_PATH, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC), "%s", name);

	written = (size > 0U) ? fs_write(&file, buf, size) : 0;
	zassert_equal(written, (ssize_t)size, "%s: wrote %zd of %zu B", name, written, size);

	zassert_ok(fs_close(&file), "%s", name);
}

static void assert_same_info(const struct wav_info *a, const struct wav_info *b, const char *name)
{
	zassert_equal(a->format, b->format, "%s: format", name);
	zassert_equal(a->num_channels, b->num_channels, "%s: channel count", name);
	zassert_equal(a->sample_rate, b->sample_rate, "%s: sample rate", name);
	zassert_equal(a->bits_per_sample, b->bits_per_sample, "%s: sample width", name);
	zassert_equal(a->data_off, b->data_off, "%s: payload offset", name);
	zassert_equal(a->data_len, b->data_len, "%s: payload length", name);
}

ZTEST_SUITE(wav_fs, NULL, wav_fs_setup, NULL, NULL, wav_fs_teardown);

ZTEST(wav_fs, test_file_and_memory_agree)
{
	ARRAY_FOR_EACH_PTR(wav_fixtures, f) {
		struct wav_source mem_src;
		struct wav_source fs_src;
		struct wav_info mem_info;
		struct wav_info fs_info;
		struct fs_file_t file;
		int mem_ret;
		int fs_ret;

		write_clip(f->buf, f->size, f->name);

		zassert_ok(wav_source_mem_init(&mem_src, f->buf, f->size), "%s", f->name);
		mem_ret = wav_parse(&mem_src, &mem_info);
		zassert_equal(mem_ret, f->expected, "%s: memory source gave %d", f->name, mem_ret);

		fs_file_t_init(&file);
		zassert_ok(fs_open(&file, CLIP_PATH, FS_O_READ), "%s", f->name);
		zassert_ok(wav_source_fs_init(&fs_src, &file), "%s", f->name);
		zassert_equal(fs_src.size, (uint32_t)f->size, "%s: file measured %u B, not %zu",
			      f->name, fs_src.size, f->size);

		fs_ret = wav_parse(&fs_src, &fs_info);
		zassert_equal(fs_ret, mem_ret, "%s: from a file %d, from memory %d", f->name,
			      fs_ret, mem_ret);

		if (fs_ret == 0) {
			uint32_t off = 0;

			assert_same_info(&fs_info, &mem_info, f->name);

			/* And the payload itself, block by block, to the very last byte. */
			while (true) {
				uint8_t from_file[16];
				uint8_t from_memory[16];
				ssize_t got_file = wav_read(&fs_src, &fs_info, off, from_file,
							    sizeof(from_file));
				ssize_t got_memory = wav_read(&mem_src, &mem_info, off, from_memory,
							      sizeof(from_memory));

				zassert_equal(got_file, got_memory,
					      "%s: at +%u a file gave %zd, memory gave %zd",
					      f->name, off, got_file, got_memory);
				zassert_true(got_file >= 0, "%s: wav_read() failed with %zd",
					     f->name, got_file);

				if (got_file == 0) {
					break;
				}

				zassert_mem_equal(from_file, from_memory, (size_t)got_file,
						  "%s: payload differs at +%u", f->name, off);
				off += (uint32_t)got_file;
			}

			zassert_equal(off, fs_info.data_len, "%s: short payload", f->name);
		}

		zassert_ok(fs_close(&file), "%s", f->name);
	}
}

ZTEST(wav_fs, test_short_file)
{
	static const uint8_t five_bytes[] = {'R', 'I', 'F', 'F', 0};
	struct wav_source src;
	struct fs_file_t file;

	write_clip(five_bytes, sizeof(five_bytes), "five_bytes");

	fs_file_t_init(&file);
	zassert_ok(fs_open(&file, CLIP_PATH, FS_O_READ));
	zassert_ok(wav_source_fs_init(&src, &file));
	zassert_equal(src.size, 5);
	zassert_equal(wav_parse(&src, NULL), -EINVAL);
	zassert_ok(fs_close(&file));
}

ZTEST(wav_fs, test_unopened_file)
{
	struct wav_source src;
	struct fs_file_t file;

	fs_file_t_init(&file);

	/* Whatever the file system makes of this, it has to be an error rather than a source
	 * that looks usable.
	 */
	zassert_true(wav_source_fs_init(&src, &file) < 0, "an unopened file was accepted");
}

ZTEST(wav_fs, test_null_arguments)
{
	struct wav_source src;
	struct fs_file_t file;

	fs_file_t_init(&file);

	zassert_equal(wav_source_fs_init(NULL, &file), -EINVAL);
	zassert_equal(wav_source_fs_init(&src, NULL), -EINVAL);
}

#endif /* CONFIG_WAV_FS */
