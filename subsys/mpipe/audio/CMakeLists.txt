#
# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0
#

if(CONFIG_MPIPE_AUDIO)
  # zephyr-keep-sorted-start
  zephyr_library_sources_ifdef(CONFIG_MPIPE_AUDIO_DAC_SINK mpipe_dac_sink.c)
  # zephyr-keep-sorted-stop
endif()
