# MUF — the Matroshka Universal File, its core

**One universal record format.** The core is the same for every use; a **profile** says which
streams and groups a device writes. **MUF has no versions** (§12).

## 1 The file

Blocks of **512 B**, a sector each, every number **little-endian**, every block ending in a
**CRC-32** (IEEE's, as zlib's `crc32`) of its bytes 0–507 in bytes 508–511. The file's room taken
whole at its start; a file stops under 2 GiB and goes on in its next part, each part begun with
the head blocks and the set's table again.

## 2 The mark

Bytes 0–3 of every head block: **`MUF` and the profile's letter** (a bare `.muf` is a niche
format's — Alpha Five's temporary license file, ProtoMuck's MUF; the four-letter ones clear of it as
far as known). The file's extension the same four.

## 3 The kinds of block

| kind | the block |
|---|---|
| 0x01 | the head |
| 0x02 | the head continued — more groups |
| 0x03 | the end |
| 0x04 | the set's table, CSV |
| 0x05 | the information stream — the device and the time, not the measurement |
| 0x06 | the constants — calibrations and the like, written once after the head |
| 0x07 | **the index** — after every 200 blocks: their kinds and steps |
| **0x10–0x1F** | **data streams 0–15** — fast, slow, slower, irregular; each at its own rate |

A reader that does not know a kind skips it.

## 4 Every block but the head: its first 12 bytes

| bytes | the field | what for |
|---|---|---|
| 0 | the kind u8 | |
| 1 | the steps in it u8 | the stream's steps in this block; 0 for the end and the table |
| 2–3 | the session u16 | a block known even out of its file |
| 4–7 | the block's number u32 | one count over every block after the head blocks; a gap is a block lost |
| 8–11 | its first step's number u32 | in its own stream, from the session's start |
| 12–27 | the profile's | zeros if none |

Then the steps, from byte 28 to at most 507.

## 5 The head (kind 0x01)

| bytes | the field | what for |
|---|---|---|
| 0–3 | the mark | `MUF` + the profile's letter |
| 4 | the format u8 | 1, for ever — no versions (§12) |
| 5 | the kind u8 | 0x01 |
| 6–7 | the session u16 | |
| 8 | the unit's number u8 | in its set; 0 none |
| 9–16 | the chip's ID u64 | the unit itself |
| 17–19 | the firmware u8 × 3 | major, minor, patch |
| 20 | the streams u8 | how many the table below holds, 18 at most |
| 21 | the groups u8 | in all, in this block and its continuations |
| 22 | the head's blocks u8 | 1 + its continuations |
| 23 | the time's source u8 | 0 the unit's own crystal, 1 a GNSS's pulse, 2 a radio's sync, 3 an outside clock |
| 24–31 | the session's start u64, µs since 1970, UTC | the instant every stream's step 0 shares; 0 unknown |
| 32 | the time zone i8, quarter hours from UTC | |
| 33–48 | the unit's name, 16 B, ASCII | |
| 49–63 | free | zeros |
| 64–207 | **the streams**, 8 B each, 18 at most | below |
| 208–495 | the first 8 groups, 36 B each | below |
| 496–507 | free | zeros |

**At most 255 steps a block** (a u8), so a stream of very small steps fills only part of a
block — values of one rate are better put together in one step, many groups to it.

**A stream's line (8 B):** the kind u8 (0x05, 0x06, 0x10–0x1F); the rate u32, **mHz** — 0 irregular;
the steps a block u8; a step's bytes u8; flags u8 (bit 0: every step carries its own time).

**The head continued (kind 0x02):** bytes 0–3 the mark, 4 the format (1), 5 the kind, 6–7 the session,
8–11 free, **12–479 thirteen groups**, 480–507 free.

## 6 A group (36 B)

A group describes a run of values in a step: its type, its scale and offset, its name and unit.

| bytes | the field | |
|---|---|---|
| 0 | the stream's kind u8 | where its values lie |
| 1 | the first u8 | its first value's place in a step, counted in values |
| 2 | the count u8 | its values |
| 3 | the type, 1 ASCII letter | as Python's `struct` and ArduPilot's DataFlash: `b`/`B` 1 byte, `h`/`H` 2, `i`/`I` 4, `q`/`Q` 8 — small signed, capital unsigned; `e` a float of 2 bytes (IEEE half), `f` of 4, `d` of 8 — measurements rather as whole numbers with a scale, exact and even; **no 1-byte float** (the OCP's FP8 is an AI's format with no letter of `struct`'s and two significant digits at best — a `b`/`B` with a scale does better) |
| 4 | flags u8 | bit 0 big-endian (a ModBus register as it rode); the rest 0 |
| 5 | exp10 i8 | |
| 6–7 | mantissa i16 | 0 counts as 1 |
| 8–11 | add i32 | |
| 12–19 | the name, 8 B, ASCII | NUL-padded |
| 20–27 | the unit, 8 B, ASCII | NUL-padded; none for a byte of fields |
| 28 | ref u8 | a reference group, its number + 1, 0 none — a group measuring the same, against which a codec may store the difference |
| 29–30 | the range u16 | what the sheet promises, in the unit |
| 31–35 | free | zeros |

**physical = (raw + add) × mantissa × 10^exp10**. A calibration beyond it
— an accelerometer's six faces, a magnetometer's matrix, a sensor's response — rides in the
**constants** stream (kind 0x06), described there by groups of its own.

**A step's bytes:** its stream's groups in the order of their *first*, each its count × its type's
size, no gap. **A value missing:** for a signed type its least (−128, −32 768, …), for an unsigned
one its greatest (255, 65 535, …), for a float NaN. An exporter reads a step with `struct`'s
letters as they stand: `'<' + letters` (`'>'` for a group flagged big-endian). **A stream with no
groups** is bytes for the profile's own decoder — a device's raw frames, a bus's blocks.

## 7 Time

**Every stream's step 0 is the session's start.** A step n of a stream of rate r (mHz) falls
n × 1000 / r seconds after it, by the unit's clock. An **irregular** stream (rate 0) carries its
own time in each step: a group named **`T_STEP`**, u32, the step of stream 0x10 it fell in. The
**information stream** ties the unit's clock to true time by three groups of reserved names:
**`TIME_UTC`** u32, s since 1970 — a whole second of true time; **`TIME_STP`** u32 — the step of
stream 0x10 it fell in; **`TIME_NS`** i32 — its place in that step, ns. Whatever gives the second —
a GNSS's pulse, a radio's sync, an outside clock — is the head's byte 23.

## 8 The set's table (kind 0x04) and the end (kind 0x03)

**The set's table:** the common 12 B, then the table's whole length u16 at
12–13, the text in 14–507, UTF-8, CSV with semicolons (RFC 4180's quoting), as many blocks as it
needs; its fields the profile's.

**The end:** the common 12 B, then: 12 why u8 — **1–15 the core's**: 1 a command, 2 the button,
3 the cell low, 4 the memory full, 5 a fault, 6 the time set out; **16 and on the profile's**;
13–15 free; 16–19 the steps of stream 0x10 u32; 20–23 the blocks u32; 24–27 the blocks dropped u32
— lost when the memory paused longer than the buffer bore.

## 9 Order

**A session's length:** a step's number is a u32 — at 1 kHz some 49 days, at 200 Hz some 248
days; a unit that runs longer (a standing station) begins a new session, a new file, before it
wraps — every day or every file, as its profile says. A block's number, a u32 too, counts 512 B
sectors: 2 TiB.

Blocks follow as they fill, the streams interleaved; a reader sorts each stream by its first
step. **A block may hold fewer steps than its stream allows** — its byte 1 says how many; the bytes
past them are zeros; the stream's next block goes on from the next step, by its first step's
number, so a part-filled block leaves no gap; a file may even be **taken up again** after a stop, its
blocks going on from the last good one with the steps' numbers that the time elapsed gives — a
jump in the numbers is a pause, with no data in it — a slow or irregular stream's block
is written at least every few seconds (the profile says how often), so that a cut cell loses
little. **The constants (kind 0x06)** are one step, no time, written once after the head and the
table. A file without its end is read to its last block whose CRC holds.

## 10 Left out, and why

- **Compression:** not wanted — a card holds enough, and a damaged block stays readable alone.
- **A layout changed mid-file:** the record stops and a new file begins with the new head —
  nothing more needed.
- **COBS framing, so a reader resyncs anywhere:** not needed — every block is one sector at a
  fixed place, beginning with its kind, session and number and ending in its CRC; a reader finds
  the next block at the next 512 B boundary and knows it whole or damaged.
- **A description from the file's end, for a search by time:** not needed — the blocks are of one
  size and carry their first step's number, so a step is a binary search over the blocks
  themselves.
- **A file a series per address:** in MUF a file per unit and session, its streams inside.

## 11 The index (kind 0x07)

On a large card the slow streams lie scattered among the fast ones; to pick one stream out — a
long flight's GNSS track — a reader would read every block. **After every 200 numbered blocks the
unit writes an index block**: their kinds and steps, and where each stream stood at the range's
start — so a reader maps the file and every block's time reading one sector in 201 (some 40 MB of
an 8 GB card). A search by time alone needs no index: a binary search of some twenty reads.

| bytes | the field | what for |
|---|---|---|
| 0–11 | the common 12 B | kind 0x07, steps 0, the session, its own number, its first step's number 0 |
| 12–15 | the first block it lists u32 | its own number − 200 |
| 16–27 | free | zeros |
| 28–95 | **each stream's next step u32 × 17** | at the range's start: the data streams and the information stream, in the head's order (the constants, timeless, left out); zeros past the last — a fixed place, used or not |
| 96–495 | **the 200 blocks, 2 B each** | the kind u8, the steps in it u8, in their order |
| 496–507 | free | zeros |
| 508–511 | CRC-32 | |

From a stream's next step and the steps of its blocks in order, every block's first step — and so
its time — is known without reading it. The index blocks are counted among the numbered blocks; a
file's last range, shorter, gets its index just before the end.

**Why at all:** MUF is not offered to the world as one more standard — it is Project-NIC's one way of
writing on the units, so that one firmware writer, one reader and one viewer serve every project;
what leaves the projects is exported to the fields' own standards — CSV, ULog, miniSEED, IAGA-2002,
EDF, C3D, GPX.

## 12 What never changes — the core's rules of stability

So that a change in one project never forces a change in all:

1. **Nothing in the core is ever moved or given a new meaning.** A field, a kind, a type letter,
   a reserved name keeps its place and sense for ever.
2. **The core only grows, and only where it left room:** a new block kind in a number not yet
   used; a new type letter; a new reserved group name; bits of a flags byte now zero; bytes now
   marked *free*. **Free bytes are written as zeros and read as nothing**, so a later use of them
   is seen by a zero turned non-zero.
3. **A reader skips what it does not know** — a kind, a group whose type letter is new to it, a
   stream with no groups — and reads the rest.
4. **No versions.** Everything a file holds is described in the file itself — its streams, its
   groups, their types and scales — so a reader needs no version to read it, and meets what is new
   to it only as something to skip. Byte 4 of the head stays 1 for ever. A change that would
   break rule 1 is not MUF any more but another format, under another mark.
5. **A project changes only its profile** — its streams, its groups, its table's fields, its end
   reasons from 16 on — and that never touches the core or another project.

## 13 What it is made of — a patchwork, openly

MUF invents little; it is stitched from formats and conventions that are open to anyone, each
named here with what was taken:

| from | what MUF took | how open |
|---|---|---|
| Project-NIC's earlier record formats | the 512 B blocks with a CRC each, the head describing groups, the set's table, the end, the file's room taken at the start; a value's description — (raw + add) × mantissa × 10^exp10, the 8 B name and unit, the reference channel | Project-NIC's own, MIT |
| EDF, the European Data Format | each signal described in the file's own head — label, dimension, range | an open specification, free to use |
| PX4's ULog | a head describing every kind of record, streams at their own rates, timestamps | an open specification of a BSD-licensed project |
| ArduPilot's DataFlash, Python's `struct` | one ASCII letter a value's type | open documentation; a common convention |
| NetCDF / the CF conventions | a scale and an offset beside each value | open standards |
| zlib's CRC-32 (IEEE 802.3) | the check of every block | a public algorithm |
| RFC 4180 | the set's table as CSV | an open RFC |

Formats and conventions are ideas, not code: taking them is free. What MUF adds is only their
sum, fitted to small units writing to a card.

