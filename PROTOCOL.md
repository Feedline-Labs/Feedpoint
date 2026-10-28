# Feedpoint protocol, version 2

The host starts Feedpoint and talks to it over its standard input (requests)
and standard output (responses). Standard error carries human-readable
diagnostics only. Requests are handled one at a time, in order; every request
gets exactly one response. All integers are little-endian.

## Request

| Bytes | Field                                   |
|-------|-----------------------------------------|
| 1     | operation (u8)                          |
| 4     | argument length in bytes (u32, max 64)  |
| n     | argument                                |

Operations that take a value send one u32 (4 bytes).

## Response

| Bytes | Field                          |
|-------|--------------------------------|
| 4     | status (i32)                   |
| 4     | payload length in bytes (u32)  |
| n     | payload                        |

Status is librtlsdr's return value for the call (0 = success; negative values
are librtlsdr/libusb error codes), or one of Feedpoint's own codes:

| Status | Meaning                                 |
|--------|-----------------------------------------|
| -9001  | Unknown operation                       |
| -9002  | Bad request (missing argument)          |
| -9003  | No device is open                       |
| -9004  | A device is already open                |
| -9005  | Out of memory                           |

Strings in payloads are a u32 byte length followed by the bytes (no
terminator).

## Operations

| Op | Name               | Argument               | Payload on success                      | librtlsdr call                    |
|----|--------------------|------------------------|-----------------------------------------|-----------------------------------|
| 1  | Hello              | —                      | u32 protocol version, string driver path, string driver SHA-256 (lowercase hex) | —                                 |
| 2  | List               | —                      | u32 count, then per device: string vendor, string product, string serial | `rtlsdr_get_device_count`, `rtlsdr_get_device_usb_strings` |
| 3  | Open               | u32 device index       | —                                       | `rtlsdr_open`                     |
| 4  | Close              | —                      | —                                       | `rtlsdr_close`                    |
| 5  | SetSampleRate      | u32 Hz                 | —                                       | `rtlsdr_set_sample_rate`          |
| 6  | SetCenterFrequency | u32 Hz                 | —                                       | `rtlsdr_set_center_freq`          |
| 7  | SetTunerBandwidth  | u32 Hz (0 = automatic) | —                                       | `rtlsdr_set_tuner_bandwidth`      |
| 8  | SetAgcMode         | u32 0 / 1              | —                                       | `rtlsdr_set_agc_mode`             |
| 9  | SetTunerGainMode   | u32 0 = auto, 1 = manual | —                                     | `rtlsdr_set_tuner_gain_mode`      |
| 10 | SetTunerGain       | i32 tenths of a dB     | —                                       | `rtlsdr_set_tuner_gain`           |
| 11 | ResetBuffer        | —                      | —                                       | `rtlsdr_reset_buffer`             |
| 12 | ReadSync           | u32 byte count         | the bytes read (interleaved 8-bit I/Q)  | `rtlsdr_read_sync`                |

Operations 4–12 need an open device. One Feedpoint process owns at most one
device; run one process per dongle.

## Lifetime

- Feedpoint exits when its standard input closes, closing the device first.
  A host that exits therefore never leaves the dongle held.
- If a call hangs (for example a stalled dongle), the host may simply kill the
  process; the operating system releases the device.
- Exit codes before the first request: 2 = driver not found, 3 = driver is
  missing an expected entry point.
