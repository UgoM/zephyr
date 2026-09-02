# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(wav)

target_sources(app PRIVATE src/main.c)

# The clip is generated rather than committed: binary files are not allowed in the tree.
# Swap in a .wav of your own here and nothing in src/main.c has to change.
set(clip_wav ${CMAKE_CURRENT_BINARY_DIR}/clip.wav)
set(clip_inc ${CMAKE_CURRENT_BINARY_DIR}/clip.inc)

add_custom_command(
  OUTPUT ${clip_wav}
  COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/gen_clip.py ${clip_wav}
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/scripts/gen_clip.py
  COMMENT "Generating a WAV container for the sample to parse"
  )

generate_inc_file_for_target(app ${clip_wav} ${clip_inc})
target_include_directories(app PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
