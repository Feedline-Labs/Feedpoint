# Feedpoint protocol

Protocol version 2, as Hello reports it. Feedpoint 1.0 and 1.1 both use it;
see [Compatibility](#compatibility) for how the protocol grows.

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

Operations that take values send them as u32s (4 bytes each), in the order
listed. GetIndexBySerial sends a string.

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

| Op | Name | Argument | Payload on success | librtlsdr call | Since |
|---|---|---|---|---|---|
| 1 | Hello | — | u32 protocol version, string driver path, string driver SHA-256 (lowercase hex) | — | 1.0 |
| 2 | List | — | u32 count, then per device: string vendor, string product, string serial | `rtlsdr_get_device_count`, `rtlsdr_get_device_usb_strings` | 1.0 |
| 3 | Open | u32 device index | — | `rtlsdr_open` | 1.0 |
| 4 | Close | — | — | `rtlsdr_close` | 1.0 |
| 5 | SetSampleRate | u32 Hz | — | `rtlsdr_set_sample_rate` | 1.0 |
| 6 | SetCenterFrequency | u32 Hz | — | `rtlsdr_set_center_freq` | 1.0 |
| 7 | SetTunerBandwidth | u32 Hz (0 = automatic) | — | `rtlsdr_set_tuner_bandwidth` | 1.0 |
| 8 | SetAgcMode | u32 0 / 1 | — | `rtlsdr_set_agc_mode` | 1.0 |
| 9 | SetTunerGainMode | u32 0 = auto, 1 = manual | — | `rtlsdr_set_tuner_gain_mode` | 1.0 |
| 10 | SetTunerGain | i32 tenths of a dB | — | `rtlsdr_set_tuner_gain` | 1.0 |
| 11 | ResetBuffer | — | — | `rtlsdr_reset_buffer` | 1.0 |
| 12 | ReadSync | u32 byte count | the bytes read (interleaved 8-bit I/Q) | `rtlsdr_read_sync` | 1.0 |
| 13 | GetVersion | — | string Feedpoint version (for example `1.1.0`) | — | 1.1 |
| 14 | GetTunerGains | — | u32 count, then i32 gains (tenths of a dB), ascending | `rtlsdr_get_tuner_gains` | 1.1 |
| 15 | GetTunerGain | — | i32 tenths of a dB | `rtlsdr_get_tuner_gain` | 1.1 |
| 16 | GetSampleRate | — | u32 Hz | `rtlsdr_get_sample_rate` | 1.1 |
| 17 | GetCenterFrequency | — | u32 Hz | `rtlsdr_get_center_freq` | 1.1 |
| 18 | GetTunerType | — | u32 tuner type (below) | `rtlsdr_get_tuner_type` | 1.1 |
| 19 | SetFrequencyCorrection | i32 ppm | — | `rtlsdr_set_freq_correction` | 1.1 |
| 20 | GetFrequencyCorrection | — | i32 ppm | `rtlsdr_get_freq_correction` | 1.1 |
| 21 | SetBiasTee | u32 0 / 1 | — | `rtlsdr_set_bias_tee` | 1.1 |
| 22 | SetBiasTeeGpio | u32 GPIO, u32 0 / 1 | — | `rtlsdr_set_bias_tee_gpio` | 1.1 |
| 23 | SetDirectSampling | u32 0 = off, 1 = I, 2 = Q | — | `rtlsdr_set_direct_sampling` | 1.1 |
| 24 | GetDirectSampling | — | u32 0 / 1 / 2 | `rtlsdr_get_direct_sampling` | 1.1 |
| 25 | SetOffsetTuning | u32 0 / 1 | — | `rtlsdr_set_offset_tuning` | 1.1 |
| 26 | GetOffsetTuning | — | u32 0 / 1 | `rtlsdr_get_offset_tuning` | 1.1 |
| 27 | SetXtalFrequency | u32 RTL2832 Hz, u32 tuner Hz | — | `rtlsdr_set_xtal_freq` | 1.1 |
| 28 | GetXtalFrequency | — | u32 RTL2832 Hz, u32 tuner Hz | `rtlsdr_get_xtal_freq` | 1.1 |
| 29 | SetTunerIfGain | u32 stage, i32 tenths of a dB | — | `rtlsdr_set_tuner_if_gain` | 1.1 |
| 30 | GetIndexBySerial | string serial | u32 device index | `rtlsdr_get_index_by_serial` | 1.1 |
| 31 | GetDeviceName | u32 device index | string name | `rtlsdr_get_device_name` | 1.1 |
| 32 | GetUsbStrings | — | string vendor, string product, string serial | `rtlsdr_get_usb_strings` | 1.1 |

Operations 1, 2, 3, 13, 30 and 31 work without an open device; every other
operation needs one. One Feedpoint process owns at most one device; run one
process per dongle.

Notes from librtlsdr, passed through unchanged:

- SetFrequencyCorrection returns -2 when the value is already set.
- SetOffsetTuning returns -2 on R820T and R828D tuners, which do not support
  it.
- SetTunerIfGain changes only E4000 tuners; other tuners accept it and do
  nothing.
- SetXtalFrequency: 0 for the RTL2832 keeps its current value; 0 for the
  tuner makes it the same as the RTL2832.
- GetIndexBySerial returns -2 when no dongle is plugged in and -3 when no
  dongle has that serial. Dongles can share a serial; the first match wins.

Tuner types: 0 = unknown, 1 = E4000, 2 = FC0012, 3 = FC0013, 4 = FC2580,
5 = R820T, 6 = R828D.

## Compatibility

Operation numbers, arguments and payloads never change; new operations take
the next number. A host written for an earlier release keeps working. The
protocol version changes only for a change that would break existing hosts.

Feedpoint 1.0 can answer an operation it does not know with -9002 or -9003
instead of -9001. A host that needs operations added after 1.0 calls
GetVersion first: any non-zero status means Feedpoint 1.0, and only
operations 1–12 are available.

## Lifetime

- Feedpoint exits when its standard input closes, closing the device first.
  A host that exits therefore never leaves the dongle held.
- If a call hangs (for example a stalled dongle), the host may simply kill the
  process; the operating system releases the device.
- Exit codes before the first request: 2 = driver not found, 3 = driver is
  missing an expected entry point.
