/*
 * Copyright (c) 2026 Ugo Marchand
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The file system byte source lives in its own translation unit so that the parser proper
 * never pulls in <zephyr/fs/fs.h>, and so that an application with no file system pays
 * nothing for this.
 */

#include <errno.h>
#include <stdint.h>

#include <zephyr/audio/wav.h>
#include <zephyr/fs/fs.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(wav, CONFIG_WAV_LOG_LEVEL);

static int wav_fs_read(void *user_data, uint32_t off, void *dst, size_t len)
{
	struct fs_file_t *file = user_data;
	uint8_t *out = dst;
	int ret;

	ret = fs_seek(file, (off_t)off, FS_SEEK_SET);
	if (ret < 0) {
		LOG_ERR("seek to +%u failed: %d", off, ret);
		return ret;
	}

	while (len > 0U) {
		ssize_t got = fs_read(file, out, len);

		if (got < 0) {
			LOG_ERR("read of %zu B at +%u failed: %zd", len, off, got);
			return (int)got;
		}

		if (got == 0) {
			/*
			 * The caller only ever asks for ranges inside the size reported by
			 * wav_source_fs_init(), so hitting the end here means the file shrank
			 * underneath us.
			 */
			LOG_ERR("file ended %zu B early at +%u", len, off);
			return -EIO;
		}

		out += got;
		len -= (size_t)got;
	}

	return 0;
}

int wav_source_fs_init(struct wav_source *src, struct fs_file_t *file)
{
	off_t size;
	int ret;

	if ((src == NULL) || (file == NULL)) {
		return -EINVAL;
	}

	/*
	 * fs_tell() alone cannot size a file, so seek to the end to measure it. Doing it here
	 * rather than on every read keeps the parser's bounds checking on a value that is read
	 * once, and reports a file system that cannot seek up front instead of part way
	 * through a parse.
	 */
	ret = fs_seek(file, 0, FS_SEEK_END);
	if (ret < 0) {
		LOG_ERR("cannot seek this file: %d", ret);
		return ret;
	}

	size = fs_tell(file);
	if (size < 0) {
		LOG_ERR("cannot size this file: %d", (int)size);
		return (int)size;
	}

	if ((uint64_t)size > (uint64_t)UINT32_MAX) {
		LOG_ERR("file is %lld B, larger than a 32-bit offset", (long long)size);
		return -EFBIG;
	}

	src->read = wav_fs_read;
	src->user_data = file;
	src->size = (uint32_t)size;

	return 0;
}
