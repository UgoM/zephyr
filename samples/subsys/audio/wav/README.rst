.. zephyr:code-sample:: wav
   :name: WAV container parser

   Read the format out of a .wav baked into flash and stream its payload.

Overview
********

This sample uses the :ref:`wav_api` library to parse a RIFF/WAVE container that has been
built into the firmware image, report what it holds, and then read its audio payload the way
a player would: a block at a time, in whole frames.

Nothing in :file:`src/main.c` is told the sample rate, the channel count or where the audio
begins. All three are read from the container's header at run time, so swapping the clip
changes the reported values without a source change.

The clip is generated at build time by :file:`scripts/gen_clip.py` and turned into a C array
by ``generate_inc_file_for_target()``. It carries a ``LIST``/``INFO`` chunk between the format
and the payload, which is what every common encoder writes and why the audio starts at offset
82 rather than the offset 44 that so much example code assumes. Point the ``add_custom_command``
in :file:`CMakeLists.txt` at a ``.wav`` of your own to try another file.

The sample measures the peak amplitude of the payload instead of playing it, so that it runs
anywhere. Deciding what is playable is left to the application: the library reports 24-bit,
eight-channel and floating-point containers just as happily, and this sample rejects anything
that is not 16-bit PCM because that is all its measurement loop understands.

Requirements
************

None beyond a board with a console. The library does no allocation, needs no driver and does
not touch any hardware.

Building and Running
********************

.. zephyr-app-commands::
   :zephyr-app: samples/subsys/audio/wav
   :host-os: unix
   :board: native_sim
   :goals: run
   :compact:

Sample Output
=============

.. code-block:: console

   *** Booting Zephyr OS build v4.5.0 ***
   PCM, 1 channel(s), 8000 Hz, 16-bit
   payload: 3200 bytes at offset 82, 1600 frames of 2 bytes, 200 ms
   streamed 3200 bytes in 13 blocks, peak amplitude 26213/32767
   sample finished
