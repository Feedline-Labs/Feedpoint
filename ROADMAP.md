# Feedpoint roadmap

Feedpoint 1.0.0 exposes the 12 operations AirPulse needs. The driver Feedpoint
ships (osmocom rtl-sdr `84195f1`) has more that a typical SDR app expects.
This roadmap fills that out, using only functions already in that driver: no
new libraries and no new drivers.

## 1.0.0 — released

Hello, List, Open, Close, SetSampleRate, SetCenterFrequency,
SetTunerBandwidth, SetAgcMode, SetTunerGainMode, SetTunerGain, ResetBuffer,
ReadSync. See [PROTOCOL.md](PROTOCOL.md).

## 1.1 — the rest of the tuner controls

Everything in this release adds to the protocol. Operations 1–12 keep their
numbers, arguments and payloads, so existing hosts keep working. New
operations get the next free number in the order they are merged.

**First, with no change in behavior:** replace the range checks in `handle()`
(`needsValue`, `operation >= OpClose`) with one table row per operation
(number, needs an open device, argument). After that, adding an operation is
one row plus one `case`.

**Protocol:** the version goes to 3, so a host can tell the new operations
exist. Arguments may be more than one u32, or a string (u32 length + bytes),
still 64 bytes at most.

| Operation | Argument | Payload | librtlsdr call |
|---|---|---|---|
| GetVersion | — | string Feedpoint version | — |
| GetTunerGains | — | u32 count, then i32 gains (tenths of a dB) | `rtlsdr_get_tuner_gains` |
| GetTunerGain | — | i32 tenths of a dB | `rtlsdr_get_tuner_gain` |
| GetSampleRate | — | u32 Hz | `rtlsdr_get_sample_rate` |
| GetCenterFrequency | — | u32 Hz | `rtlsdr_get_center_freq` |
| GetTunerType | — | u32 tuner type | `rtlsdr_get_tuner_type` |
| SetFrequencyCorrection | i32 ppm | — | `rtlsdr_set_freq_correction` |
| GetFrequencyCorrection | — | i32 ppm | `rtlsdr_get_freq_correction` |
| SetBiasTee | u32 0 / 1 | — | `rtlsdr_set_bias_tee` |
| SetBiasTeeGpio | u32 gpio, u32 0 / 1 | — | `rtlsdr_set_bias_tee_gpio` |
| SetDirectSampling | u32 0 = off, 1 = I, 2 = Q | — | `rtlsdr_set_direct_sampling` |
| GetDirectSampling | — | u32 | `rtlsdr_get_direct_sampling` |
| SetOffsetTuning | u32 0 / 1 | — | `rtlsdr_set_offset_tuning` |
| GetOffsetTuning | — | u32 | `rtlsdr_get_offset_tuning` |
| SetXtalFrequency | u32 RTL Hz, u32 tuner Hz | — | `rtlsdr_set_xtal_freq` |
| GetXtalFrequency | — | u32 RTL Hz, u32 tuner Hz | `rtlsdr_get_xtal_freq` |
| SetTunerIfGain | u32 stage, i32 tenths of a dB | — | `rtlsdr_set_tuner_if_gain` |
| GetIndexBySerial | string serial | u32 device index | `rtlsdr_get_index_by_serial` |
| GetDeviceName | u32 device index | string name | `rtlsdr_get_device_name` |
| GetUsbStrings | — | string vendor, product, serial of the open device | `rtlsdr_get_usb_strings` |

**Testing:** every operation is tested against a real dongle, never a mock:
set a value, read it back with the matching Get, and confirm the IQ still
looks right. Because the driver does not change, AirPulse SNR should not
change either; spot-check it against 1.0.0 on the same hardware before
release.

## 1.2 — library build and mobile

Feedpoint builds two ways from the same code:

- **Library** (`.dll` / `.so` / `.dylib`, static for iOS): all operation
  handling, behind one entry point that takes a request and returns a
  response in the [PROTOCOL.md](PROTOCOL.md) format. PROTOCOL.md stays the one
  specification for both builds.
- **Executable**: a thin loop that reads requests from standard input, calls
  the library and writes responses to standard output. Hosts that use
  Feedpoint today see no change.

This opens platforms where a separate program won't work:

- **Android:** the app loads the library. USB permission comes from Android
  as a file descriptor, so this needs a driver that accepts one and an Open
  operation that passes it in.
- **iPad:** apps cannot start other programs, so the app links the static
  library.

Mobile drivers come in through the driver rules in
[CONTRIBUTING.md](CONTRIBUTING.md), including the SNR comparison.

## 2.0 — gapless streaming

`ReadSync` asks for one block at a time, and the dongle sits idle between
requests, so samples are lost in the gaps. That is fine for SNR snapshots but
not for decoding (audio, APRS, ADS-B). 2.0 adds StartStream and StopStream:
between them Feedpoint sends data frames continuously, using
`rtlsdr_read_async`.

This changes the protocol from strictly one request and one response, and it
needs a second thread in Feedpoint (operating system threads only, no
library). It will be designed in an issue before any code is written.

## Not planned

- `rtlsdr_write_eeprom` and `rtlsdr_read_eeprom`: writing can brick a dongle.
- `rtlsdr_set_testmode`.
- Signal processing, decoding, a network server, or any library beyond the
  approved driver. These belong in the host program or in a fork.
