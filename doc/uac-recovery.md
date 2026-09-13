# UAC audio recovery

This fork repairs UAC audio startup and recovery in the archived upstream baseline
`5552c365bfb319eed7cbbf6300a67028ab70db9e`.

## Failure mechanism

The upstream driver links capture and playback with `snd_pcm_link()`, but never
explicitly starts capture. Its capture callback checks sample availability before
calling `snd_pcm_mmap_readi()`. A freshly prepared capture stream has no samples,
and its descriptor normally does not become readable until capture starts. It
therefore depends on playback reaching its start threshold. Missing playback RTP
or silence suppression can prevent the other audio direction from starting.

The same dependency recurs after an overrun or underrun. Recovery prepares both
streams, including the healthy direction, and leaves capture waiting for playback
again. Suspended streams are not recovered. Idle calls leave the streams running,
so the next call can inherit stale samples or an XRUN.

The playback threshold also contains two incorrect clamp branches: normal 20 ms
framing requires 250 ms of prefill. Thresholds are calculated before ALSA's actual
buffer and period sizes are known. Unsupported sample rates and some invalid
capture configurations can be accepted because initialization errors are lost.

## Intended behavior

- Capture starts explicitly when a call becomes the UAC sound source, before
  Asterisk begins waiting for its descriptor. Playback remains independently
  driven by incoming media.
- Recovery prepares only the affected stream and explicitly restarts capture.
  Suspend recovery uses a bounded prepare fallback rather than a resume loop
  that could sleep while holding the device lock.
- Releasing or holding the current UAC sound source stops its audio. A late
  notification for a previous sound source cannot stop the current one.
- Initialization rejects unsupported rates, invalid channel counts and missing
  capture poll descriptors. Playback thresholds use negotiated geometry and a
  corrected 100–250 ms clamp, bounded by the actual buffer.
- Last-channel resource cleanup runs after the channel count reaches zero.
  Disconnect traversal also tolerates the current call being freed during
  release, avoiding a use-after-free during device reconnection.
- Channels that are waiting or local do not poll the active capture descriptor.
  A capture activation that still fails after bounded transient retry explicitly
  rejects the call instead of advertising a working silent channel.

These changes preserve the existing modem AT command sequence and SMS/dialplan
interfaces. They do not change Telegram forwarding or SIP/RTP configuration.

## Validation boundary

Software regression tests exercise ALSA state transitions, initialization errors,
call activation and media callbacks. A successful build or mocked ALSA test does
not establish EC20 hardware quality. Validate both media directions on each modem,
including an inbound call with no initial outbound speech, hold/resume, and a
new call after idle time. Check SMS forwarding separately after any module reload.

Nonblocking playback has a bounded latency policy. Positive short writes are
continued from the remaining sample offset, but a persistent `EAGAIN` can still
drop the unwritten suffix. There is no new unbounded media queue or retry loop.
USB protocol errors, disappearing devices, firmware behavior and network RTP loss
remain separate diagnostic paths.

## Primary references

- [ALSA PCM states, errors and transfer contracts](https://www.alsa-project.org/alsa-doc/alsa-lib/pcm.html)
- [ALSA PCM implementation](https://github.com/alsa-project/alsa-lib/blob/master/src/pcm/pcm.c)
- [Linux PCM readiness implementation](https://github.com/torvalds/linux/blob/master/sound/core/pcm_native.c)
