.. zephyr:code-sample:: sam-v71-xult-wm8904-rtp-playback
   :name: WM8904 RTP playback
   :relevant-api: rtp i2s_interface audio_codec_interface

   Play an RTP audio stream received over Ethernet through the on-board WM8904
   codec.

Overview
********

The sample joins an RTP multicast group, buffers the stream it receives and
plays it through the WM8904 codec on the SAM V71 Xplained Ultra. It expects
L16 as defined by :rfc:`3551`: big endian signed 16-bit samples, two channels
interleaved, 24 kHz, no payload header.

The rate is not a free choice. The codec master clock comes from PMC
programmable clock PCK2, which divides PLLACK by an integer, and the codec
clocks a whole number of its own SYSCLK periods per frame. With the 12 MHz
crystal on this board, 24 kHz is the highest rate the chain reaches exactly.
The sender has to be configured to match.

Nothing corrects the drift between the sender's sample clock and the board's.
The jitter buffer is deep enough that it takes minutes to run dry, and the
occupancy figure the sample reports every second says which way it is going and
how fast:

.. code-block:: console

   [00:00:12.000,000] <inf> main: buffer 128 ms, packets 1204, underruns 0

A figure that holds steady means the two clocks agree closely enough to ignore.
One that walks towards either end is the measurement to feed into a correction:
resampling on this side, or retuning the codec FLL.

Requirements
************

* A :zephyr:board:`sam_v71_xult` board, revision B, with headphones or powered
  speakers in the codec output jack.
* Ethernet connected to a network with a DHCP server, on the same segment as
  the sender. A switch between the two has to pass the multicast group, which
  IGMP snooping can block.

Building and Running
********************

.. zephyr-app-commands::
   :zephyr-app: samples/boards/atmel/sam_v71_xult/wm8904_rtp_playback
   :board: sam_v71_xult/samv71q21b
   :goals: build flash
   :compact:

Sending from PipeWire
=====================

PipeWire needs no code to feed this: ``module-rtp-sink`` is a sink node that
transmits RTP. Distributions ship the defaults under :file:`/usr/share/pipewire/`
and read user drop-ins from a directory that has to be created first::

   mkdir -p ~/.config/pipewire/pipewire.conf.d

Place the following in
:file:`~/.config/pipewire/pipewire.conf.d/99-samv71-rtp.conf`:

.. code-block:: none

   context.modules = [
     { name = libpipewire-module-rtp-sink
       args = {
         destination.ip = "239.0.1.1"
         destination.port = 5004
         sess.name = "samv71"
         audio.format = "S16BE"
         audio.rate = 24000
         audio.channels = 2
         audio.position = [ FL FR ]
         stream.props = {
           node.name = "samv71"
           node.description = "SAM V71 Xult"
           media.class = "Audio/Sink"
         }
       }
     }
   ]

Restart PipeWire, then select the sink::

   systemctl --user restart pipewire pipewire-pulse
   pactl set-default-sink samv71

``media.class`` is what makes the module an output device applications can be
routed to. Without it the module captures from another node instead of offering
itself as a sink, and fails with ``stream error: no target node available``.

If the sink does not appear, check that the module is installed at all:
:file:`/usr/lib/pipewire-0.3/libpipewire-module-rtp-sink.so`.

PipeWire resamples whatever the applications produce down to 24 kHz on the host.

The payload type the sender picks for L16 is dynamic. The sample accepts any
payload type, so it does not have to be matched, but ``tcpdump -X udp port
5004`` is the way to confirm packets are leaving the host at all.

Sending from FFmpeg
===================

Useful to separate a board problem from a PipeWire one:

.. code-block:: console

   ffmpeg -re -i track.flac -af aresample=24000 -acodec pcm_s16be -ac 2 \
          -f rtp -pkt_size 1440 rtp://239.0.1.1:5004
