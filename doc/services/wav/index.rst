.. _wav_api:

WAV
###

Overview
********

RIFF/WAVE, the container behind the :file:`.wav` extension, wraps an audio payload in a chain of
tagged chunks. The WAV library walks that chain and reports where the audio starts, how long it is
and how it is laid out, so that an application does not have to convert its audio on a host and
ship it as a raw array.

The library parses; it does not decode, resample, mix or play. It reports what the file declares
and applies no output-device policy, which keeps it independent of any audio driver: deciding that
a 24-bit eight-channel container is unplayable belongs to the application, which is the only party
that knows what its hardware accepts.

The audio bytes are reached through a caller-supplied read callback rather than a pointer, so the
same code path serves a container linked into flash and a container in a file. Header parsing is
sparse random access over small windows — 12 bytes of preamble, 8 bytes per chunk header and at
most 40 bytes of format chunk — and never touches the payload.

The container is treated as untrusted input throughout: every declared chunk size is clamped to
what the source actually holds, the number of chunks inspected is bounded by
:kconfig:option:`CONFIG_WAV_MAX_CHUNKS`, and :c:struct:`wav_info` is written only when parsing
succeeds.

There is no dynamic allocation and no static state; a parse costs about forty bytes of stack.

Usage
*****

To use the WAV API, enable :kconfig:option:`CONFIG_WAV` and include the header file:

.. code-block:: c

   #include <zephyr/audio/wav.h>

Parsing a container in memory
=============================

.. code-block:: c

   /* A .wav linked into the image, header included. */
   static const uint8_t clip[] = { ... };

   struct wav_source src;
   struct wav_info info;
   int ret;

   ret = wav_source_mem_init(&src, clip, sizeof(clip));
   if (ret < 0) {
           return ret;
   }

   ret = wav_parse(&src, &info);
   if (ret < 0) {
           printk("not a usable .wav: %d\n", ret);
           return ret;
   }

   printk("%u channel(s), %u Hz, %u-bit, %u ms\n", info.num_channels, info.sample_rate,
          info.bits_per_sample, wav_duration_ms(&info));

Parsing a container in a file
=============================

With :kconfig:option:`CONFIG_WAV_FS` enabled, an open file works as a source. Only the two lines
that set the source up differ; everything after them is identical.

.. code-block:: c

   struct fs_file_t file;
   struct wav_source src;
   struct wav_info info;
   int ret;

   fs_file_t_init(&file);

   ret = fs_open(&file, "/lfs/clip.wav", FS_O_READ);
   if (ret < 0) {
           return ret;
   }

   ret = wav_source_fs_init(&src, &file);
   if (ret == 0) {
           ret = wav_parse(&src, &info);
   }

The file must stay open for as long as the source is used: :c:func:`wav_read` seeks and reads
through it on every call.

Reading the payload
===================

:c:func:`wav_read` takes an offset relative to the start of the audio, clamps the request to the
end of the payload and returns the number of bytes produced, so the same refill loop works for
every source. It is byte oriented; keeping the offset and length multiples of
:c:func:`wav_frame_size` is what keeps the blocks frame aligned.

.. code-block:: c

   uint8_t block[256];
   uint32_t off = 0;
   size_t len = ROUND_DOWN(sizeof(block), wav_frame_size(&info));

   while (true) {
           ssize_t got = wav_read(&src, &info, off, block, len);

           if (got <= 0) {
                   /* Negative is an error, zero means the payload is consumed. */
                   return (int)got;
           }

           /* Hand block[0 .. got - 1] to the audio output here. */

           off += (uint32_t)got;
   }

Writing a custom source
=======================

Any byte range that can be read at an absolute offset can back a parse: fill in
:c:struct:`wav_source` directly with a :c:type:`wav_read_cb`, an opaque pointer for it and the
total number of bytes available. The parser never asks for more than
:c:macro:`WAV_SOURCE_MAX_READ_SIZE` bytes in one call while parsing the header, and never asks for
a range outside the declared size, so a callback needs no bounds checking of its own and no
buffer larger than that.

Configuration
*************

Related configuration options:

* :kconfig:option:`CONFIG_WAV`
* :kconfig:option:`CONFIG_WAV_FS`
* :kconfig:option:`CONFIG_WAV_MAX_CHUNKS`
* :kconfig:option:`CONFIG_WAV_LOG_LEVEL`

API Reference
*************

.. doxygengroup:: wav
