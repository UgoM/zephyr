# SPDX-License-Identifier: Apache-2.0

zephyr_sources_ifdef(CONFIG_WAV    wav.c)
zephyr_sources_ifdef(CONFIG_WAV_FS wav_fs.c)
