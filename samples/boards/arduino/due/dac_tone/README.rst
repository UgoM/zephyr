.. zephyr:code-sample:: dac-tone
   :name: DAC tone
   :relevant-api: dac_interface

   Play a continuous sine tone on the Arduino Due DAC using the DAC
   driver's blocking write value API.

Overview
********

This sample plays a continuous 440 Hz tone on the Arduino Due DAC0
analog output pin (PB15, pin 21 of the bottom 24-pin header) at a
48 kHz sample rate. Connect an amplifier and a loudspeaker to the DAC0
pin to hear the tone.

The tone is synthesized with a 256-entry lookup table driven by a
32-bit phase accumulator. The phase advances by a fixed precomputed
increment per sample, so the frequency is exactly 440 Hz at a 48 kHz
sample rate. Samples are paced against the DWT CPU cycle counter,
which reads at full MCK resolution and is cheap enough to poll without
introducing jitter; ``k_cycle_get_32()`` is too slow to poll at this
rate.

Building and Running
********************

This sample does not require extra configuration. Build and flash with:

.. zephyr-app-commands::
   :zephyr-app: samples/boards/arduino/due/dac_tone
   :board: arduino_due
   :goals: build flash
   :compact: