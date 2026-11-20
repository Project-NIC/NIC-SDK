# MLF — the Matroshka Log File, its core

**One universal log container.** Where MUF ([`MUF.md`](MUF.md)) is static — blocks of one size at
fixed places, steps of a fixed size, a layout fixed for the file — MLF is dynamic: **records of any
length, written as they come, coded by a codec chosen per segment, and the description of what they
are allowed to change in the middle of a file.** It suits a source that writes unevenly for months —
a sensor that pours frames beside one that changes a value an hour, a bus whose blocks differ in
length. **The core is universal, the same for every use; what is particular to one use is its
profile** (§10), **named by the fourth letter of the mark** (for example `H` – Heimdall, `S` –
Strelka, …; `U` where none is known), and each program brings its own glue to read it.
**MLF has no versions** (§11).

## 1 The file

```
offset 0                                                             1 MB × 2^size
┌────────┬───────────────────────────┬────────┬───────────────────────────┬────────┐
│ HEADER │ DATA → segments, configs  │ unused │ ← DESCRIPTION, 16 B each  │ MIRROR │
└────────┴───────────────────────────┴────────┴───────────────────────────┴────────┘
```

- **A file is 1 MB × 2^`size` — 1, 2, 4 … 128 MB — taken whole when it opens**, so a FAT or exFAT
  table is touched only then; the unwritten part reads as zeros. The size is the profile's choice
  per series, and the file records its own in the header.
- **The header fills whole sectors of 512 B**, zeros after its CRC to the sector's end, and **its
  mirror — the same bytes — fills the file's last sectors**, written when the file opens, as
  NIC-Arduino's MLA kept its prefix twice: a bad first sector does not blind the file. A reader whose
  header's CRC fails takes the mirror: the fewest sectors *k* at the file's end that begin with the
  mark, whose length fits in *k* sectors and whose CRC holds. The configs are not mirrored — they
  are in the data, each with its own CRC, and the next file's header carries them.
- **The data grows up from the header's last sector, the description grows down from the
  mirror.** The file is full
  when the next object's framed bytes and its entry would cross, and the next file of the series
  opens. **The core closes a file on nothing else.** **The next file's header carries the sections
  in force** — the header's as every config before changed them — so every file is read alone.
- **A series** is the run of files one source writes, numbered by the header's sequence; whose it is,
  the header's source says (§2).
- **Everything is little-endian.** The header, every segment and every config end in a **CRC-32**
  (IEEE 802.3, the one zlib's `crc32` computes) over every byte before it; a description entry ends
  in a **CRC-16-CCITT** — polynomial 0x1021 from 0xFFFF, unreflected, no final XOR — so a free
  entry's zeros fail it.
- **Every second in a file is a u32 on one continuous scale**, the profile's (§10) — the header's,
  the segments', the configs' and the entries' alike.

## 2 The header

| bytes | the field | |
|---|---|---|
| 0–3 | the mark | `MLF` and the profile's letter, ASCII — `U` where no profile is known |
| 4 | the format u8 | 1, for ever — no versions (§11) |
| 5 | size u8 | the file is 1 MB × 2^size, 0…7 |
| 6–7 | the sequence u16 | the file's number in its series |
| 8–11 | the length u32 | the whole header, the mark to the CRC |
| 12–15 | the first second u32 | the second the file opened |
| 16–19 | the source u32 | whose series it is, read as the profile says; 0 none |
| 20… | the sections | until the length less 4 |
| end | CRC-32 | over every byte before it |

## 3 Sections

**A section** is `id 1 B · reserved 3 B · length 4 B · body`, the length the body's. A reader skips
an id it does not know.

| id | whose | |
|---|---|---|
| 1–254 | **the profile's** | what the records are: their layout, the source's settings, … (§10); the mark's letter says whose |
| 255 | `CODEC` | `codec 1 B · revision 1 B · length 2 B · table` — the table a codec needs to decode, for a codec that needs one (§8); NRC needs none and writes none |

## 4 The data — segments and configs

**A segment is one write of one series' records**, coded by a codec:

| bytes | the field | |
|---|---|---|
| 0–3 | sync | `MSEG` |
| 4–7 | the length u32 | the sync to the CRC |
| 8–11 | the first second u32 | the oldest record's |
| 12–15 | the last second u32 | the newest's |
| 16 | the codec u8 | 0 raw · 1 NRC ([`NRC.md`](NRC.md)) |
| 17 | the time u8 | 0 the records carry their time, as the profile says · 1 an even run |
| 18–19 | the place u16 | an even run's first record in its first second, on the profile's grid; 0 otherwise |
| 20… | the body | |
| end | CRC-32 | over every byte before it |

**What a record is, the profile says**; codec 0 is the records back to back, as the profile writes
them. **The codecs are a set**: the writer codes a segment with every codec it has and keeps the
shortest body, the lower codec on a tie; a codec added later takes the next number, and a reader
skips a segment whose codec it does not know. **Every segment is read alone**: no codec carries
state from one segment to the next.

**The time, either way, segment by segment** (as NIC-Arduino's MLA kept it once, in its log): a
segment whose records come unevenly — some kept, some not, a late repair — carries each record's
time inside it, as the profile writes it (time 0). **An even run** (time 1) carries none: its records
follow one another on the profile's grid of so many a second, the first at the segment's first
second and the place — the record *i* at the first second + (place + *i*) / the grid — so the time
is written once, in the segment and its entry. **A gap closes an even run**; the next record opens a
segment of its own. A writer chooses per segment; a file holds both.

**A config** is written when a section of the header changes, and holds from the second it names:

| bytes | the field | |
|---|---|---|
| 0–3 | sync | `MCFG` |
| 4–7 | the length u32 | the sync to the CRC |
| 8–11 | the from second u32 | |
| 12–15 | reserved | zeros |
| 16… | sections | as in the header |
| end | CRC-32 | over every byte before it |

**A config carries only the sections that changed, and every id it carries replaces all the
sections of that id in force.** A reader applies, at each record's second, the last config at or
before it over the header. **A change does not close the file.** A segment is coded with the
sections in force where it lies, so a writer writes its buffer out as a segment before it writes a
config that changes what the codec reads.

## 5 Framing — COBS

**Every segment and every config is written COBS-encoded** — no zero byte inside it — **and followed
by one 0x00**; the cost is at most one byte in 254. The encoding is Cheshire and Baker's: each run of
up to 254 non-zero bytes behind a code byte one more than its length, 0xFF a run with no zero after
it, and an object that ends on a full run closed by an empty one (0x01). A reader dropped anywhere
in the data — a damaged entry, a file whose description is lost, a stream cut short — skips to the
next zero and reads on.

## 6 The description — 16 B entries from the end

| bytes | the field | |
|---|---|---|
| 0–3 | the second u32 | a segment: its last second · a config: its from second |
| 4–7 | the offset u32 | its first COBS byte in the file |
| 8–11 | the length u32 | its COBS bytes without the zero; the next object starts one byte past them |
| 12 | the type u8 | 1 segment · 2 config |
| 13 | reserved | zero |
| 14–15 | CRC-16 | over the 14 bytes before |

Entry *i* lies at the mirror's start less 16 × (*i* + 1). **Entries are of one stride and their seconds
never go back**, so a second is a binary search over the description of a live file or a full one
alike. A free entry reads all zeros and fails its CRC; an entry inside the good range that fails
its CRC is a damaged object, its second taken as the last good one's before it.

## 7 Writing order

**The data first, then its entry**: an entry never points at bytes not yet written. On starting, a
writer finds each series' last file, searches its description for the last good entry, and goes on
writing behind that entry's object. Nothing is repaired, truncated or rebuilt.

## 8 Carrying its own description

**Nothing a reader needs is outside the file and its medium.**

- **The medium carries the format in words**: `MLF.txt` at its root, UTF-8 — this document,
  [`NRC.md`](NRC.md), the profile, and the description of every codec the writer uses, written when
  the medium is prepared and again when a codec is added. Whoever finds the medium in fifty years
  writes a reader from the text: the descriptions are exact to the bit, and no code is needed.
- **A codec that needs a table carries it in the file**: a `CODEC` section in the header, and in a
  config from the second it changes, so a table changed by a later firmware never orphans a file
  written with the old one.

## 9 MLF beside MUF

| | MUF | MLF |
|---|---|---|
| the unit of writing | a 512 B block at a fixed place | a segment of any length, COBS-framed |
| the data | steps of a fixed size, streams at fixed rates | records of any length, written as they come |
| a change of layout | the file ends, a new one begins | a config, the file goes on |
| compression | none | a codec chosen per segment |
| a search by time | over the blocks | over the description from the end |
| a file | one unit, one session | a series per source, a file running for months |

## 10 What the profile says

- **The source**: how bytes 16–19 of the header are read.
- **The scale of the seconds**: its epoch and how leap seconds are kept.
- **Its letter** in the mark, and **its sections, ids 1–254**, and what each config of them means.
- **The record**: its fields and its length, as codec 0 writes it.
- **How NRC reads the records**: the series it cuts them into, their widths, signs and references,
  and the order of the parts in the stream ([`NRC.md`](NRC.md), §7).
- **The file's size per series, the files' names and paths**, and when old files are given up.

## 11 What never changes — the core's rules of stability

As MUF's (§12 there):

1. **Nothing in the core is ever moved or given a new meaning.**
2. **The core only grows, and only where it left room**: a codec number after 1, a type of entry after 2, bytes now *reserved* — **written as zeros and read as nothing**.
3. **A reader skips what it does not know** — a section, a codec, an object whose CRC fails — and
   reads the rest.
4. **No versions.** Byte 4 of the header stays 1 for ever; what a file holds is described in it. A
   change that would break rule 1 is not MLF any more but another format, under another mark.
5. **A profile changes only itself** — its sections, its records, its reading of the source.

## 12 What it is made of

| from | what MLF took | how open |
|---|---|---|
| Project-NIC's earlier archive format | the whole: the file taken at once, data up and description down, segments and configs, the codec set, the format's text on the medium | Project-NIC's own, MIT |
| COBS, Cheshire and Baker (IEEE/ACM Trans. on Networking, 1999) | the framing | a published algorithm |
| zlib's CRC-32 (IEEE 802.3); CRC-16-CCITT | the checks | public algorithms |
