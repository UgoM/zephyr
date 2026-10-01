.. zephyr:code-sample:: uac2-dac
   :name: USB Audio 2 DAC sample
   :relevant-api: usbd_api uac2_device audio_codec_interface mpipe

   USB Audio 2 playback sample streaming audio from a USB host to the internal DAC.

Overview
********

This sample demonstrates how to implement USB Audio Class 2 (UAC2) playback on a
board with an internal DAC. It presents itself to the USB host as a Full-Speed
USB Audio 2.0 class device supporting 48 kHz 16-bit monaural playback.

The audio received over USB travels through an mpipe pipeline to an audio codec
device backed by the internal DAC. A hardware timer triggers the DAC and a
cyclic DMA transfer feeds it, so the CPU never touches an individual sample. No
external I2S or codec hardware is required; the analog audio signal is available
directly from the DAC output pin.

The sample is intentionally minimal: it supports a single sample rate
(48 kHz), a single resolution (16-bit) and a single channel (mono). It is
configured for the :zephyr:board:`nucleo_h745zi_q` board, whose DAC output 1 is
available on pin PA4.

Requirements
************

* A :zephyr:board:`nucleo_h745zi_q` board
* A USB host (e.g. a Linux, Windows or macOS PC)

Building and Running
********************

The code can be found in :zephyr_file:`samples/subsys/usb/uac2_dac`.

To build and flash the application:

.. zephyr-app-commands::
   :zephyr-app: samples/subsys/usb/uac2_dac
   :board: nucleo_h745zi_q/stm32h745xx/m7
   :goals: build flash
   :compact:

After flashing, connect the board to the USB host with a USB cable (the ST-Link
USB port is not used, use the USB OTG FS connector). The device will enumerate
as a USB Audio 2.0 'speaker'.

Feeding Audio
*************

Any application that can play audio to a USB sound card can be used to feed the
sample. The audio stream must be 16-bit, 48 kHz, mono. Note that the device
does not mix channels, so stereo streams will be interleaved when fed directly
to the DAC; play a mono stream for correct output.

On Linux, the device typically appears as ``hw:2,0`` (check with ``aplay -l``).
For example, to play a mono 48 kHz 16-bit wave file:

.. code-block:: console

   aplay -D hw:2,0 -t wav /path/to/file.wav

The resulting analog signal can be observed on PA4 (DAC_OUT1) with an
oscilloscope, or amplified into a speaker/headphone for audible output.

Implementation Details
**********************

The sample uses the new USB device stack (``USB_DEVICE_STACK_NEXT``) with the
USB Audio 2.0 class driver (:kconfig:option:`CONFIG_USBD_AUDIO2_CLASS`). The
UAC2 audio function is described in the devicetree overlay as follows:

* Clock source: internal, programmable, single sampling frequency of 48 kHz
* Input terminal: USB streaming, mono (front-left)
* Output terminal: speaker
* AudioStreaming interface: 16-bit subslot, 16-bit resolution

The playback path is an mpipe pipeline::

   app_src --> queue --> dac_sink --> audio codec device

Audio data flows from the USB host to the DAC as follows:

#. The UAC2 class driver requests a receive buffer from the application
   (``get_recv_buf`` callback).
#. When the USB host sends isochronous audio packets (96 bytes, i.e. 48 mono
   16-bit samples, every 1 ms SOF frame), the class driver invokes the
   ``data_recv_cb`` callback.
#. The application pushes the packet into the pipeline's ``app_src`` element
   without blocking; a packet the pipeline cannot take right away is dropped.
#. The ``queue`` element hands the buffer to its own thread, so the USB context
   never waits on the DAC.
#. The ``dac_sink`` element writes the buffer with :c:func:`audio_codec_write`,
   waiting for the codec to report a free block first. This is what applies
   backpressure to the queue.
#. The codec driver converts the signed 16-bit little-endian samples to the
   DAC's 12-bit left-aligned format and drops them into the half of its cyclic
   DMA buffer that is not being played.
#. TIM6 triggers the DAC at the sample rate and the DMA controller feeds it one
   sample per trigger. A block that was not written in time is played again,
   which is audible as a click.

Because the timer and the DMA controller do all the pacing, the system tick is
left at its default value and no interrupt fires per sample.

Explicit feedback is implemented with a constant value of 48 samples per SOF
frame (Q10.14 encoding, ``48 << 14``). This tells the host to send the nominal
number of samples every frame. Because the DAC sample clock is not phase locked
to the USB SOF clock, long-running streams may slowly drift between buffer
overrun and underrun; the constant-feedback design keeps the drift limited to
one sample periodically and is acceptable for this minimal sample.

Limitations
***********

* Fixed sample rate (48 kHz), fixed resolution (16-bit) and fixed channel
  count (mono). The UAC2 descriptors advertise a single sampling frequency,
  so the host cannot change the rate.
* Constant explicit feedback is used, so long-running streams may slowly drift
  between buffer overruns and underruns. Each event is handled in software
  (dropped packets on overflow, a repeated block on underrun).
* No volume/mute control is implemented (no feature unit).
