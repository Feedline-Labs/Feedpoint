# Feedpoint streaming — design (2.0, see [ROADMAP](../ROADMAP.md))

Status: **design only, nothing built, not scheduled.**

There is no real audio problem today: AirPulse Listen audio through Feedpoint is excellent. This
design is held in reserve, to build only if a need shows up — for example choppy audio on a slow
node such as a busy Pi, or AirPulse on Android.

## 1. Why it might be needed

Since AirPulse moved to Feedpoint, Listen audio has an occasional tiny blip. It is rare and minor;
this section explains where it most likely comes from, so the fix is ready if it ever matters.

How Listen reads today (AirPulse `ListenCamp` → `FeedpointClient.ReadSync` → Feedpoint
`handleReadSync`):

1. AirPulse asks for 100 ms of IQ (about 180 KB at 900 kS/s).
2. Feedpoint calls `rtlsdr_read_sync` until the block is full.
3. Feedpoint writes the 180 KB to stdout (`fwrite` + `fflush`) and only then reads the next request.

AirPulse already double-buffers and asks for the next block right away, so the host side is
pipelined. Feedpoint is not: **during step 3 nothing reads the dongle.** `rtlsdr_read_sync` only
pulls samples while a call is in progress, so while a block crosses the pipe the dongle's small
buffer can overflow and samples are lost. Most gaps are too short to hear. When the pipe or CPU is
briefly slow (GC, a busy Pi), the gap grows and you hear a blip. Before Feedpoint, AirPulse called
librtlsdr in-process, so the same gap was much shorter — which fits the blips starting with
Feedpoint.

This is the most likely cause, **not yet proven**. Step 0 proves or disproves it before any C is
written.

## 2. Step 0 — confirm the cause first

Two checks, both cheap:

1. **Listen on the node's own page (LAN, no internet).** Blips there mean the dongle → Feedpoint →
   AirPulse path. Blips only through Ride Along / Radio point at the internet path instead (section 8),
   and this design would not fix them.
2. **Time the gap in AirPulse.** Log the time from one `ReadSync` returning to the next being sent,
   and count blocks where it exceeds a few ms. Long gaps that line up with audible blips confirm it.

If neither check shows the Feedpoint gap, **do not build this**: fix whatever the checks point to.

## 3. Design

Two worker threads and a ring of preallocated blocks between them. The main thread keeps reading
requests from stdin, as now.

```
 dongle ──► Reader thread ──► [ ring: N preallocated blocks ] ──► Writer thread ──► stdout ──► AirPulse
                (rtlsdr_read_sync loop)                          (frames)
```

**Reader:** loops `rtlsdr_read_sync(block)` straight into the next free slot. It **never waits on the
writer**, so the dongle is never left unread except for the microseconds between two calls.

**Writer:** at stream start waits until `prefill` blocks are full, then sends the oldest full block as
a frame. If the ring is empty it waits for the reader. It can never get ahead of the reader.

**Ring:** `slots` blocks of `blockBytes`, all allocated at `StartStream`. Default 10 slots
(1 second of audio at 100 ms blocks, about 1.8 MB). Plenty of headroom; the reader only needs the
next slot.

**If the writer falls behind** (host slow, ring full): the reader must not block — blocking is
exactly the gap we are removing. It overwrites the oldest unsent block and counts it as dropped.
With 10 slots this means the host was stalled for about a second; the count makes it visible.

**If the reader falls behind** (USB hiccup): the writer simply waits for the next block.

## 4. Threads: created once, parked between streams

Listen starts and stops a stream on every stay on a frequency, so threads must **not** be created
and destroyed per stream.

- Both threads are created **once**, the first time a stream starts, and live until `Close` or
  Feedpoint exits.
- Between streams they are **parked**: blocked on a condition variable, using no CPU.
- `StartStream` wakes them; `StopStream` parks them again.
- On `Close` / exit: set `quit`, wake both, join both, free the ring.

**Locking rules** (where threaded code usually goes wrong):

- One mutex guards only the ring indexes and flags (`head`, `tail`, `count`, `streaming`, `quit`).
- **Never hold the mutex during `rtlsdr_read_sync` or `fwrite`** — only while moving an index.
- Two condition variables: `work` (wake parked threads) and `filled` (a block is ready for the writer).
- A slot is either owned by the reader (being filled), or full (waiting for the writer), or being
  written. The writer copies nothing: it writes straight from the slot, then marks it free.
- While a stream runs, **the writer thread owns stdout**. The main thread writes to stdout only
  after both workers are parked.

**Portability:** Feedpoint is C with no outside libraries, so a small `thread.h` shim (about 50 lines)
wraps the OS APIs: `CreateThread` / `SRWLOCK` / `CONDITION_VARIABLE` on Windows, pthreads elsewhere.

## 5. Protocol additions (2.0)

New operations only. Operations 1–32 keep their numbers, arguments and payloads, so **a host written
for 1.x runs unchanged against Feedpoint 2.0**: it simply never calls the new operations (the
Compatibility rule in PROTOCOL.md). Nothing in older releases changes.

| Op | Name | Argument | Response |
|---|---|---|---|
| 33 | StartStream | u32 block bytes, u32 slots, u32 prefill | status 0, then frames until StopStream |
| 34 | StopStream | — | status 0, payload: u32 frames sent, u32 blocks dropped |

**Frames** use the normal response header with status **1** (`STATUS_FRAME`), so a host can always
tell a frame from the final StopStream response (status 0):

| Bytes | Field |
|---|---|
| 4 | status = 1 (frame) |
| 4 | payload length |
| 4 | sequence number (u32, from 0 per stream) |
| 4 | blocks dropped so far in this stream (u32) |
| n | IQ bytes (interleaved 8-bit I/Q), as ReadSync returns |

Gaps in the sequence number or a growing dropped count tell the host exactly when samples were lost.

**While streaming** only StopStream is accepted; any other request gets **-9006** (Streaming), so
nobody retunes underneath a running stream. StopStream with no stream running returns **-9007**
(Not streaming).

**StopStream order:** main thread sets `streaming = false` → reader finishes its current
`rtlsdr_read_sync` (at most one block, 100 ms) → writer finishes the frame it is sending → both park
→ main thread writes the StopStream response. The host reads frames until it sees status 0.

## 6. AirPulse changes

- `FeedpointClient`: `StartStream`, `ReadFrame` (reads one frame into the caller's buffer, returns
  sequence and dropped), `StopStream`.
- `ListenCamp`: StartStream when a stay begins, ReadFrame in the loop (replacing ReadNextIqAsync),
  StopStream before the scanner retunes. Log any sequence gap or dropped increase.
- **Scan stays on ReadSync**, unchanged: it retunes constantly and reads short windows, where
  streaming brings nothing.
- **Fallback:** if `GetVersion` reports below 2.0, Listen keeps using ReadSync as today.

## 7. Cost

- Memory: slots × block bytes, default 10 × 180 KB ≈ 1.8 MB, allocated once per stream start.
- Latency: `prefill` blocks. Default 2 = +200 ms before the first audio of a stay. The browser
  already buffers 300 ms, so the total stays under about half a second.
- CPU: the same reads and writes as today, now on two threads instead of one.

## 8. The internet path and the browser

This design only covers **dongle → Feedpoint → AirPulse**, on the node. A bigger ring here does not
help a slow home internet connection, because that is a different link with its own buffers:

| Link | Buffer today |
|---|---|
| AirPulse → Next (node's upload) | the node's Ride Along outbox |
| Next → each listener | per-listener outbox, 256 frames (about 25 s); a slow listener loses its oldest frames and never slows anyone else |
| Browser | 300 ms jitter buffer before playback (`opusPlayer.ts`, `jitterSeconds`) |

**Does the browser need the same reader/writer design?** No. It already separates the two jobs:
WebSocket frames arrive on the network side, and playback is scheduled ahead on the Web Audio clock
from a 300 ms buffer, which is the browser's version of prefill. It is single-threaded by nature and
needs no threads.

What could help listeners on poor connections is an **adaptive** jitter buffer: start at 300 ms,
grow (for example to 600–800 ms) after a dropout, and shrink back slowly once the connection is
steady. That is a small change in `opusPlayer.ts`, separate from this design, and worth doing only if
blips show up on remote listening but not on the node's own page (Step 0, check 1).

## 9. Tests (live dongle, named after the operations)

- **StartStream:** stream 60 s; sequence numbers have no gaps; dropped = 0.
- **StopStream:** response arrives within one block time; frames sent matches the frames received.
- **StartStream / StopStream 100 times in a row:** no thread created after the first start, memory flat.
- **Streaming rejects other operations:** SetCenterFrequency during a stream returns -9006.
- **Slow host:** pause reading frames for 2 s; dropped increases; the stream recovers on its own.

## 10. Open questions

1. Default slots (10) and prefill (2): confirm after Step 0 numbers.
2. Should a later version use `rtlsdr_read_async` in the reader instead of a `read_sync` loop?
   It keeps USB transfers queued even between calls, closing the last microsecond gap. The
   `read_sync` loop is simpler, uses the entry point Feedpoint already loads, and should be enough;
   revisit only if Step 0 still shows drops after this lands.
