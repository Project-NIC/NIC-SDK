★ N.I.C. ★

# NIC-SDK

<p align="center"><img src="pictures/NIC-SDK.jpg" width="600" alt="SDK"/></p>

**Project-NIC's shared libraries, formats and tools** — one home for what more than one project
uses, so that it is written once and never copied: each project takes a tagged version and pins
it.

[![License: MIT](https://img.shields.io/badge/License-MIT-red.svg)](https://opensource.org/licenses/MIT)
![Version: 0.3](https://img.shields.io/badge/version-0.3-blue.svg)

## What is in it

| | what |
|---|---|
| [`MUF.md`](MUF.md) | **MUF**, the record format — blocks of a sector, each with its CRC; streams at their own rates; every stream and value described in the file's head; a profile on its core for each device |
| [`muf/muf.c`](muf/muf.c), [`muf/muf.h`](muf/muf.h) | **MUF's writer in C**, for any unit: the head and its continuations, the set's table, the constants, every stream's blocks as they fill, pauses as jumps in the steps, an index after every 200 numbered blocks, the end |
| [`muf/muf_read.c`](muf/muf_read.c), [`muf/muf_read.h`](muf/muf_read.h) | **MUF's reader in C**: through a callback — a file, a buffer, a browser — every block's CRC, the head and its groups, the values raw and physical, every type letter |
| [`tools/muf2csv.c`](tools/muf2csv.c) | MUF to CSV in C, a file a stream |
| [`tools/mufwrite.py`](tools/mufwrite.py), [`tools/muf.py`](tools/muf.py) | **the writer and the reader in Python** — the C's twins, tried on a computer with nothing compiled; the reader checks the index and writes CSV |
| [`tools/muf2gpx.c`](tools/muf2gpx.c), [`tools/muf2gpx.py`](tools/muf2gpx.py) | **MUF to GPX**, twins in C and Python, the same bytes: the position found by its description — two values in degrees, a height in metres — each point's time from the head's start and the stream's rate, a missing fix left out |
| [`tests/`](tests/) | the twins held to each other: a recorder's file of 60 s at 1 kHz written by the C and by the Python **the same byte for byte**, read by both readers to **the same CSV byte for byte**, and its content checked; a track exported to GPX by both twins, the same byte for byte |

**Two twins at one level:** C for the units, the computer and the browser (WebAssembly); Python
the same, for trying things without compiling — each held to the other.

Run the tests: `python3 tests/test_muf.py` and `python3 tests/test_gpx.py` (a C compiler and Python 3).

## To come

More exporters — ULog, miniSEED, IAGA-2002, EDF, C3D — and a viewer, each when a project
needs it.

Software under the MIT licence ([`LICENSE`](LICENSE)).

## Acknowledgements

To the AI assistant Claude (Anthropic), for its great help with this project.

★ Viva La Resistánce ★
