# Feedpoint - A Universal SDR Gateway

Open-source RTL-SDR: tune, read, return raw IQ.

Feedpoint is a small standalone program that owns one RTL-SDR dongle. A host
program starts it, sends requests on its standard input and reads responses
from its standard output. Each request is one librtlsdr call: list devices,
open, set sample rate / frequency / gain, reset the buffer, read samples.
Feedpoint does no signal processing; all timing and DSP belong to the host.

Any program that can start a process and use its pipes can use Feedpoint. The
protocol is documented in [PROTOCOL.md](PROTOCOL.md).

## Drivers

Feedpoint loads exactly one approved driver, `librtlsdr-v4.dll` / `.so` /
`.dylib`, from beside the Feedpoint executable. It never searches the
operating system's library path, so a node runs the proven driver or reports
plainly that it is missing. `RTLSDR_LIBRARY_PATH` overrides the location for
testing. The Hello response includes the driver's SHA-256 so the host can
confirm it is an approved build.

The approved drivers, their exact source code, licenses and attribution are in
[`drivers/`](drivers). On Windows, `libusb-1.0.dll` must sit beside the driver;
on Linux the driver uses the system libusb-1.0.

## Building

Open the folder in Visual Studio (File > Open > Folder) with the "Desktop
development with C++" workload, or use CMake directly. GitHub Actions builds
Windows x64, Linux x64, Linux arm64 and macOS (universal) on every push; the
builds download from the workflow run's page.

## Credits

Feedpoint builds on the work of:

- [osmocom rtl-sdr](https://gitea.osmocom.org/sdr/rtl-sdr) — librtlsdr
  (GPL-2.0-or-later), including the RTL-SDR Blog team's V4 support.
- [libusb](https://libusb.info) — USB access (LGPL-2.1-or-later).

Full attribution and license texts for the bundled drivers are in
[`drivers/README.md`](drivers/README.md).

Feedpoint is used by [AirPulse](https://feedlinelabs.com) from Feedline Labs.

## License

Copyright (C) 2026 Feedline Labs.

Feedpoint is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later
version. See [LICENSE](LICENSE).

"RTL-SDR" is a trademark of RTL-SDR Blog Ltd. Feedpoint is not affiliated with
RTL-SDR Blog Ltd or Realtek.
