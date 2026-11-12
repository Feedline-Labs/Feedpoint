# Working on Feedpoint

Feedpoint stays small on purpose: one dongle, one librtlsdr call per
operation. Anyone can build on it through the protocol without changing
it. Changes to Feedpoint itself are welcome when they keep it that way.

## What Feedpoint will always be

- **Small.** Plain C99 in `src/`. The source layout changes only for a
  [roadmap](ROADMAP.md) item.
- **No libraries.** Only the C library and the operating system's loader and
  threads. A pull request that adds a dependency or a package will be closed.
- **Only functions the driver has.** An operation calls a function that exists
  in the approved driver source (osmocom rtl-sdr `84195f1`).
- **Drivers.** New platform drivers are welcome. A driver pull request adds the
  file under `drivers/<platform>/`, its exact source and build notes, its
  SHA-256 in [`drivers/README.md`](drivers/README.md), and the swap test
  below. It is listed as **Community** until Feedline Labs confirms it.
  Existing approved drivers are never changed or replaced.

## Driver swap test

Drivers are part of the measurement: builds of librtlsdr have disagreed by
10 dB of SNR on the same hardware. A new driver must read the same as an
approved one.

1. Use one dongle, antenna and set of cables, moved between a node running an
   approved driver and the node running the new driver.
2. **Noise floor:** put a 50 Ω terminator on the antenna input and record the
   noise floor on both.
3. **Live signal:** with the antenna back on, record the SNR of a strong,
   steady local signal (a beacon or repeater), alternating between the two
   nodes back to back.
4. **Pass:** both readings within ±3 dB of the approved driver. Post the
   readings in the pull request.
- **No signal processing.** Feedpoint tunes and returns raw IQ. Demodulating,
  decoding, filtering and networking belong in the host program.

## Adding an operation

1. **Open an issue first.** Say which librtlsdr call it wraps and why a host
   needs it. Check [ROADMAP.md](ROADMAP.md); it may already be planned.
2. **Append, never renumber.** A new operation takes the next free number.
   Existing numbers, arguments and payloads never change.
3. **Document it in the same pull request:** a row in
   [PROTOCOL.md](PROTOCOL.md) and a line in the README's `Op` enum.
4. **Test it on a real dongle.** No mocks. In the pull request, say which
   dongle and operating system you used and what you saw (for example: set
   the value, read it back, IQ still looks right).
5. **Match the existing code.** Readable, no defensive layers, no new
   abstractions. One operation is a table row and a `case`.

## What happens next

Every pull request is reviewed and approved by Feedline Labs before it
merges. Releases are tagged, and AirPulse uses tagged releases only, so work
on `main` never reaches running nodes until it has been tested and released.

If you need something outside this scope, fork it. The GPL is there for that,
and we'd like to hear about what you build.
