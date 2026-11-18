#!/usr/bin/env python3
"""NIC-SDK — MUF to GPX, the twins held to each other: python3 tests/test_gpx.py
A track written with the Python writer — a position stream described as any device would, two
values in degrees and a height in metres, a missing fix among them — exported by the C and by the
Python: the same GPX byte for byte, its points checked."""
import os, struct, subprocess, sys, tempfile
here = os.path.dirname(os.path.abspath(__file__))
tools = os.path.join(here, "..", "tools")
sys.path.insert(0, tools)
import mufwrite, muf2gpx

tmp = tempfile.mkdtemp()
path = os.path.join(tmp, "track.muf")
out = open(path, "wb")
head = dict(letter="X", session=1, unit=1, chip=1, fw=(0, 2, 0), time_source=1,
            start_us=1767225600000000, tz_q=0, name="track")       # 2026-01-01 00:00:00 UTC
streams = [(0x12, 1000, 21, 12, 0)]                                # 1 Hz
groups = [dict(stream=0x12, first=0, count=2, type="i", exp10=-7, mantissa=1, name="POS", unit="deg"),
          dict(stream=0x12, first=2, count=1, type="i", exp10=-3, mantissa=1, name="ALT", unit="m")]
w = mufwrite.Writer(out.write, head, streams, groups)
for n in range(100):
    lat = -2**31 if n == 50 else 501234567 + n * 100                  # step 50: no fix
    w.step(0, struct.pack("<iii", lat, -1234567 - n * 10, 250000 + n))
w.end(1)
out.close()

exe = os.path.join(tmp, "muf2gpx")
subprocess.check_call(["cc", "-std=c11", "-D_POSIX_C_SOURCE=200809L", "-Wall", "-Wextra", "-Werror", "-O2", "-o",
                       exe, os.path.join(tools, "muf2gpx.c"), os.path.join(here, "..", "muf", "muf_read.c"), "-lm"])
subprocess.check_call([exe, path, os.path.join(tmp, "c.gpx")])
open(os.path.join(tmp, "py.gpx"), "w", newline="\n").write(muf2gpx.gpx(path))
c, p = open(os.path.join(tmp, "c.gpx")).read(), open(os.path.join(tmp, "py.gpx")).read()
fails = []
if c != p: fails.append("the C's GPX is not the Python's")
pts = [l for l in p.splitlines() if l.startswith("<trkpt")]
if len(pts) != 99: fails.append("99 points, the missing fix left out (%d)" % len(pts))
if pts[0] != '<trkpt lat="50.1234567" lon="-0.1234567"><ele>250.000</ele><time>2026-01-01T00:00:00.000Z</time></trkpt>':
    fails.append("the first point: " + pts[0])
if "<time>2026-01-01T00:01:39.000Z</time>" not in pts[-1]: fails.append("the last point's time")
print("\n".join("FAIL " + x for x in fails) or "%d points, the twins the same: all good" % len(pts))
sys.exit(1 if fails else 0)
