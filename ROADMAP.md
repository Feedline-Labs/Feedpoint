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

Operations 13–32: GetVersion, the gain list, reading settings back, frequency
correction, bias tee, direct sampling, offset tuning, crystal frequency, IF
gain, and finding dongles by serial. Each is documented in
[PROTOCOL.md](PROTOCOL.md), with its librtlsdr call.

Everything in this release adds to the protocol, so hosts written for 1.0
keep working; see Compatibility in [PROTOCOL.md](PROTOCOL.md). Each operation
is now one row in Feedpoint's operation table plus one `case`, and an
operation Feedpoint does not know always returns -9001.

**Testing:** every operation has a test named after it in
[tests/](tests/README.md), run against a real dongle, never a mock. Set
operations read their value back with the matching Get. Because the driver
does not change, AirPulse SNR should not change either; spot-check it
against 1.0.0 on the same hardware before release.

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
between them Feedpoint sends data frames continuously from a dedicated reader
thread.

This changes the protocol from strictly one request and one response, and it
needs a second thread in Feedpoint (operating system threads only, no
library). Hosts that never call the new operations keep working unchanged.

Designed in [docs/streaming-design.md](docs/streaming-design.md). **Not
scheduled:** audio through Feedpoint is excellent today, so this is held in
reserve until a need shows up (for example AirPulse on Android).

## Not planned

- `rtlsdr_write_eeprom` and `rtlsdr_read_eeprom`: writing can brick a dongle.
- `rtlsdr_set_testmode`.
- Signal processing, decoding, a network server, or any library beyond the
  approved driver. These belong in the host program or in a fork.
