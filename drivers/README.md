# Drivers

Feedpoint loads one approved librtlsdr driver per platform, from this folder.
These drivers are part of the measurement, not just plumbing: other builds of
librtlsdr reported wildly different SNR on the same SDR, antenna and cables,
so a node on Windows and a node on a Pi disagreed. Once osmocom rtl-sdr commit
`84195f1` produced consistent, accurate results, it was used for every
platform.

**Rules:** ship these files byte-for-byte unchanged. Never rebuild or replace
one without re-testing SNR against the others on the same hardware. Each file
is identified by its SHA-256; Feedpoint reports the fingerprint of the driver
it loaded, and AirPulse warns if it is not one listed here.

Every driver is named `librtlsdr-v4` on every platform. Files were renamed
only; their contents are unchanged from the builds below.

## Approved drivers

| Folder | File | SHA-256 | Built from | Status |
|---|---|---|---|---|
| `win-x64` | `librtlsdr-v4.dll` | `e8fc7a5698c0235da6a8c57677970c1d0e3cba3231ed914faf8d670686659e7c` | osmocom rtl-sdr `84195f169f5b4b7dc06a10efb1e210d02b49e51c` (v2.0.2-5), Visual Studio 2022, linked against libusb 1.0.30. Hash matches the build output. | Confirmed |
| `win-x64` | `libusb-1.0.dll` | `7cbf37e76dae9c840c7e8dbf7348ee8897dcc86c8ba45e46ada60b89411569f7` | libusb v1.0.30 (file version 1.0.30.12037). Hash matches the copy the driver was built against. | Confirmed |
| `linux-arm64` | `librtlsdr-v4.so` | `449378e3363f832ffc9a502cad4c0f572046a7bf178d267caf52ef014e010dff` | osmocom rtl-sdr `84195f1` (per Feedline Labs build notes). Uses the system libusb-1.0. | Documented |
| `linux-x64` | `librtlsdr-v4.so` | `b70e949093d6525251800bfef4a14041956958bd9a2808efd77f305a5bf1eb19` | osmocom rtl-sdr `84195f1`, built in WSL (Ubuntu 22.04) with `-DDETACH_KERNEL_DRIVER=ON`. Uses the system libusb-1.0. Formerly shipped as `librtlsdr.so`. | Documented |
| `osx` | `librtlsdr-v4.dylib` | `43c96fad22b6baed70084be65fcec79beab1518d43365e3693894512c4a380f7` | osmocom rtl-sdr `84195f1`, universal arm64 + x86_64, libusb v1.0.27 statically linked. Used for both osx-arm64 and osx-x64. Formerly shipped as `librtlsdr.dylib` and an identical `librtlsdr.0.dylib`. | Documented |

**Confirmed** = hash verified against the build output or the builder's
checksums. **Documented** = source recorded by the builder; hash not yet
matched to a build output.

Retired, no longer shipped: the Pi `librtlsdr.so` built from osmocom v0.6.0
(commit `1f0eafe6`, SHA-256 `424f388c…`). It came from a different codebase
than every other driver, so SNR from it was not comparable.

## Corresponding source

The exact source for every driver above is in [`source/`](source):

| File | Contents | SHA-256 |
|---|---|---|
| `rtl-sdr-84195f1.tar.gz` | osmocom rtl-sdr at commit `84195f169f5b4b7dc06a10efb1e210d02b49e51c` | `3c04a1ade13d1acd5c9cb4371aad90e57f6ff7785da45e90d9513c8c251490de` |
| `libusb-1.0.30.tar.bz2` | libusb v1.0.30 release source (Windows `libusb-1.0.dll`) | `fea36f34f9156400209595e300840767ab1a385ede1dc7ee893015aea9c6dbaf` |
| `libusb-1.0.27.tar.bz2` | libusb v1.0.27 release source (linked into the macOS driver) | `ffaa41d741a8a3bee244ac8e54a72ea05bf2879663c098c82fc5757853441575` |

Upstream: [osmocom rtl-sdr](https://gitea.osmocom.org/sdr/rtl-sdr)
([GitHub mirror](https://github.com/osmocom/rtl-sdr)) and
[libusb](https://github.com/libusb/libusb).

## Attribution and licenses

**librtlsdr** (osmocom rtl-sdr): turns Realtek RTL2832U-based DVB dongles into
SDR receivers. Copyright (C) 2012–2014 Steve Markgraf, Copyright (C) 2012
Dimitri Stolnikov, and the contributors listed in
[`licenses/rtl-sdr-AUTHORS.txt`](licenses/rtl-sdr-AUTHORS.txt) and in the
source. Licensed under the GNU General Public License version 2 or later; see
[`licenses/GPL-2.0.txt`](licenses/GPL-2.0.txt).

**libusb**: cross-platform user-space USB library. Copyright (C) the libusb
authors (see `AUTHORS` in the libusb source). Licensed under the GNU Lesser
General Public License version 2.1 or later; see
[`licenses/LGPL-2.1.txt`](licenses/LGPL-2.1.txt).

With thanks to the osmocom rtl-sdr developers, the libusb developers, and the
RTL-SDR Blog team, whose V4 work is part of upstream librtlsdr.

"RTL-SDR" is a trademark of RTL-SDR Blog Ltd. Feedpoint and Feedline Labs are
not affiliated with RTL-SDR Blog Ltd, osmocom or Realtek.
