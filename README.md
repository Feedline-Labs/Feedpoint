# Feedpoint

Open-source RTL-SDR dongle server: tune, read, return raw IQ.

Feedpoint is a small standalone program that owns one RTL-SDR dongle. A host
program starts it, sends requests on its standard input and reads responses
from its standard output. Each request is one librtlsdr call: list devices,
open, set sample rate / frequency / gain, reset the buffer, read samples.
Feedpoint does no signal processing; all timing and DSP belong to the host.

Any program that can start a process and use its pipes can use Feedpoint. The
protocol is documented in [PROTOCOL.md](PROTOCOL.md).

## Drivers

Feedpoint loads librtlsdr at runtime. It looks, in order:

1. The path in the `RTLSDR_LIBRARY_PATH` environment variable.
2. Beside the Feedpoint executable: `librtlsdr-v4.dll`, `librtlsdr.dll`,
   `rtlsdr.dll` (Windows); `librtlsdr-v4.so`, `librtlsdr.so`, `librtlsdr.so.0`,
   `librtlsdr.so.2` (Linux); `librtlsdr.dylib`, `librtlsdr.0.dylib` (macOS).
3. The operating system's library search path.

On Windows, `libusb-1.0.dll` must sit beside the driver. The RTL-SDR Blog fork
of librtlsdr is recommended: it is required for RTL-SDR Blog V3/V4 dongles.

## Building

Open the folder in Visual Studio (File > Open > Folder) with the "Desktop
development with C++" workload, or use CMake directly. GitHub Actions builds
Windows x64, Linux x64, Linux arm64 and macOS (universal) on every push; the
builds download from the workflow run's page.

## Credits

Feedpoint builds on the work of:

- [osmocom rtl-sdr](https://osmocom.org/projects/rtl-sdr/wiki) — the original
  librtlsdr (GPL-2.0-or-later).
- [RTL-SDR Blog](https://github.com/rtlsdrblog/rtl-sdr-blog) — the librtlsdr
  fork with V3/V4 support (GPL-2.0-or-later).
- [libusb](https://libusb.info) — USB access (LGPL-2.1-or-later).

Feedpoint is used by [AirPulse](https://feedlinelabs.com) from Feedline Labs.

## License

Copyright (C) 2026 Feedline Labs.

Feedpoint is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later
version. See [LICENSE](LICENSE).

"RTL-SDR" is a trademark of RTL-SDR Blog Ltd. Feedpoint is not affiliated with
RTL-SDR Blog Ltd or Realtek.
