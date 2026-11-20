★ N.I.C. ★

# NIC-SDK

<p align="center"><img src="pictures/NIC-SDK.jpg" width="600" alt="SDK"/></p>

**Project-NIC's shared libraries, formats and tools** — one home for what more than one project
uses, so that it is written once and never copied: each project takes a tagged version and pins
it.

[![License: MIT](https://img.shields.io/badge/License-MIT-red.svg)](https://opensource.org/licenses/MIT)
![Version: 0.5](https://img.shields.io/badge/version-0.5-blue.svg)

## What is in it

| | what |
|---|---|
| [`MUF.md`](MUF.md) | **MUF**, the record format — blocks of a sector, each with its CRC; streams at their own rates; every stream and value described in the file's head; a profile on its core for each device |
| [`muf/muf.c`](muf/muf.c), [`muf/muf.h`](muf/muf.h) | **MUF's writer in C**, for any unit: the head and its continuations, the set's table, the constants, every stream's blocks as they fill, pauses as jumps in the steps, an index after every 200 numbered blocks, the end |
| [`muf/muf_read.c`](muf/muf_read.c), [`muf/muf_read.h`](muf/muf_read.h) | **MUF's reader in C**: through a callback — a file, a buffer, a browser — every block's CRC, the head and its groups, the values raw and physical, every type letter |
| [`tools/muf2csv.c`](tools/muf2csv.c) | MUF to CSV in C, a file a stream |
| [`tools/mufwrite.py`](tools/mufwrite.py), [`tools/muf.py`](tools/muf.py) | **the writer and the reader in Python** — the C's twins, tried on a computer with nothing compiled; the reader checks the index and writes CSV |
| [`tools/muf2gpx.c`](tools/muf2gpx.c), [`tools/muf2gpx.py`](tools/muf2gpx.py) | **MUF to GPX**, twins in C and Python, the same bytes: the position found by its description — two values in degrees, a height in metres — each point's time from the head's start and the stream's rate, a missing fix left out |
| [`MLF.md`](MLF.md) | **MLF**, the Matroshka Log File — the dynamic container beside MUF: records of any length as they come, a codec chosen per segment, the description growing down from the file's end and allowed to change mid-file; a profile on its core for each use |
| [`NRC.md`](NRC.md) | **NRC**, the NIC Rice Codec — lossless integer series into one bit stream: predict, Rice-code the residue, no table; carried by an MLF segment or packing another container's data |
| [`mlf/mlf.c`](mlf/mlf.c), [`mlf/mlf.h`](mlf/mlf.h), [`nrc/nrc.c`](nrc/nrc.c), [`nrc/nrc.h`](nrc/nrc.h) | **MLF's and NRC's cores in C99**, malloc and free their only dependencies |
| [`tools/mlf.py`](tools/mlf.py), [`tools/nrc.py`](tools/nrc.py) | **the same cores in Python**, the C's twins |
| [`mseed/mseed.c`](mseed/mseed.c), [`tools/mseed.py`](tools/mseed.py) | **miniSEED 2.4** — integers as Steim-2 in 512 B records, blockettes 1000 and 1001, the quality bits, FDSN StationXML 1.2 beside; bound to no container; twins |
| [`iaga/iaga.c`](iaga/iaga.c), [`tools/iaga.py`](tools/iaga.py) | **IAGA-2002** — a magnetometer's day, minute values by INTERMAGNET's Gaussian or second values, faster samples brought to 1 Hz by a low-pass; bound to no container; twins |
| [`tools/muf2mseed.c`](tools/muf2mseed.c), [`tools/muf2mseed.py`](tools/muf2mseed.py), [`tools/muf2iaga.c`](tools/muf2iaga.c), [`tools/muf2iaga.py`](tools/muf2iaga.py) | **MUF to miniSEED and to IAGA-2002**, the values found by their description in the head; twins |
| [`tools/mlfseries.h`](tools/mlfseries.h), [`tools/mlfseries.c`](tools/mlfseries.c), [`tools/mlfseries.py`](tools/mlfseries.py) | **the socket for a program's glue**: a series of MLF files walked, the configs applied, each segment handed to the glue for its profile (below, *The glue*) |
| [`tools/mlf2mseed.c`](tools/mlf2mseed.c), [`tools/mlf2mseed.py`](tools/mlf2mseed.py), [`tools/mlf2iaga.c`](tools/mlf2iaga.c), [`tools/mlf2iaga.py`](tools/mlf2iaga.py) | **MLF to miniSEED and to IAGA-2002** through the program's glue; twins |
| [`tests/`](tests/) | the twins held to each other: a recorder's file of 60 s at 1 kHz written by the C and by the Python **the same byte for byte**, read by both readers to **the same CSV byte for byte**, and its content checked; a track exported to GPX by both twins, the same byte for byte; an MLF sample file written by the C and by the Python the same byte for byte, every series read back, random series round-tripped through NRC; MUF and MLF to miniSEED and IAGA-2002, the two twins the same bytes, ObsPy reading the records where it is installed; both containers read through their mirrors with the head destroyed |

**Two twins at one level:** C for the units, the computer and the browser (WebAssembly); Python
the same, for trying things without compiling — each held to the other.

Run the tests: `python3 tests/test_muf.py`, `test_gpx.py`, `test_mlf.py`, `test_mseed.py`,
`test_iaga.py` and `test_mlf_export.py` (a C compiler and Python 3).

## The glue

The SDK's libraries are general: they know the containers — MUF and MLF — and the formats they
export to, never what a project keeps in them. **Between a file and the libraries there is always
the program's glue**: a thin layer that knows its profile, named by the fourth letter of the mark
(for example `H` – Heimdall, `S` – Strelka, …), and tells the libraries what the tables and the
records hold. **The letter is only a shortcut**, as a FourCC beside a container: it says whose file
it is at a glance; the file still describes itself, so whoever understands it can write its glue. **The SDK carries no glue — only the libraries and the socket a glue plugs into**
(`tools/mlfseries.h` and `tools/mlfseries.py`: a channel, the station, a segment's samples). Each
program writes its own glue and keeps it in its own repository. MUF's head describes its values in
the core's own form, so its exporters read the values without help; what lies beyond — a stream
with no groups, the station's place, what Appendix A suggests — is the glue's too. The glue in
`tests/glue_example.*` is an example for a made-up profile `X`, for the tests only, not a profile
of the SDK.

## To come

More exporters — ULog, EDF, C3D — and a viewer, each when a project needs it.

Software under the MIT licence ([`LICENSE`](LICENSE)).

## Acknowledgements

To the AI assistant Claude (Anthropic), for its great help with this project.

★ Viva La Resistánce ★
