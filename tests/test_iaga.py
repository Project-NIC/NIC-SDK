#!/usr/bin/env python3
"""NIC-SDK — MUF to IAGA-2002, the twins held to each other: python3 tests/test_iaga.py
Two magnetometers' records written with the Python writer — 1 Hz for thirty hours over three UTC
days, X Y Z in one group and F in another, a pause of ten minutes and a missing value among them;
and 4 Hz for three hours, through the low-pass — exported by the C and by the Python: the same
files byte for byte. A field rising evenly comes out as its own value at every filled minute and
second; where the record is not, or too little of a filter's span, 99999.00."""
import os, struct, subprocess, sys, tempfile
here = os.path.dirname(os.path.abspath(__file__))
root = os.path.join(here, "..")
tools = os.path.join(root, "tools")
sys.path.insert(0, tools)
import mufwrite, muf2iaga

tmp = tempfile.mkdtemp()
DAY0 = 1767225600                                                   # 2026-01-01 00:00:00 UTC
fails = []


def record(name, rate, start, seconds, pause_at=None):
    path = os.path.join(tmp, name + ".muf")
    out = open(path, "wb")
    head = dict(letter="X", session=1, unit=1, chip=1, fw=(0, 5, 0), time_source=1,
                start_us=start * 1000000, tz_q=0, name="Test & Field")
    streams = [(0x11, rate * 1000, 30, 16, 0)]
    groups = [dict(stream=0x11, first=0, count=3, type="i", exp10=-2, mantissa=1, name="XYZ", unit="nT"),
              dict(stream=0x11, first=3, count=1, type="i", exp10=-2, mantissa=1, name="MAGF", unit="nT")]
    w = mufwrite.Writer(out.write, head, streams, groups)
    step = 0
    while step < seconds * rate:
        if pause_at is not None and step == pause_at * rate:
            w.skip(0, 600 * rate)
            step += 600 * rate
            continue
        x = 2000000 + step                                          # 20 000 nT rising 0.01 nT a step
        y = -150000 + (step * 7919) % 301 - 150                     # noise about −1 500 nT
        z = -2**31 if step == 5000 * rate else 4400000 + (step * 104729) % 1001 - 500
        w.step(0, struct.pack("<iiii", x, y, z, 4800000 + step // 60))
        step += 1
    w.end(1)
    out.close()
    return path


def build(src):
    exe = os.path.join(tmp, "muf2iaga")
    subprocess.check_call(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-O2", "-o", exe, src,
                           os.path.join(root, "muf", "muf_read.c"), os.path.join(root, "iaga", "iaga.c"), "-lm"])
    return exe


exe = build(os.path.join(tools, "muf2iaga.c"))


def both(path, interval, start, rate):
    cdir, pdir = os.path.join(tmp, "c"), os.path.join(tmp, "p")
    for d in (cdir, pdir):
        os.makedirs(d, exist_ok=True)
        for x in os.listdir(d):
            os.remove(os.path.join(d, x))
    args = ["BDV", "49.080", "14.016", "496", "NIC-SDK test"]
    cout = subprocess.check_output([exe, path, cdir] + args + [interval]).decode().split()
    pout = muf2iaga.export(path, pdir, *args, interval)
    names = sorted(os.path.basename(x) for x in cout)
    if names != sorted(os.path.basename(x) for x in pout): fails.append("the files' names differ")
    for n in names:
        if open(os.path.join(cdir, n)).read() != open(os.path.join(pdir, n)).read():
            fails.append("%s: the C's is not the Python's" % n)
    for n in names:
        lines = open(os.path.join(pdir, n)).read().splitlines()
        rows = [l for l in lines if l[:2] == "20"]
        if any(len(l) != 70 for l in lines if not l.startswith("20")) or any(len(l) != 70 for l in rows):
            fails.append("%s: a line not of IAGA-2002's width" % n)
        filled = 0
        for l in rows:
            t = (int(l[11:13]) * 3600 + int(l[14:16]) * 60 + int(l[17:19]))
            day = (int(l[8:10]) - 1)
            s = DAY0 + day * 86400 + t
            x = float(l[30:40])
            if x == 99999.0:
                continue
            filled += 1
            want = 20000 + 0.01 * (s - start) * rate
            if abs(x - want) > 0.006:
                fails.append("%s %s: X %.2f, the field's own %.2f" % (n, l[:19], x, want)); break
        print("%s: %d rows, %d filled" % (n, len(rows), filled))
    return names


start1 = DAY0 + 22 * 3600                                           # 1 Hz from 22:00 for 30 h
p1 = record("mag1", 1, start1, 30 * 3600, pause_at=10 * 3600)
names = both(p1, "minute", start1, 1)
if names != ["bdv20260101vmin.min", "bdv20260102vmin.min", "bdv20260103vmin.min"]:
    fails.append("the days: %r" % names)
day2 = open(os.path.join(tmp, "p", "bdv20260102vmin.min")).read().splitlines()
rows = {l[11:16]: l for l in day2 if l[:2] == "20"}
if "99999.00" not in rows["08:05"][30:40]: fails.append("the pause's minutes not 99999.00: " + rows["08:05"])
if "99999.00" in rows["08:15"][30:40]: fails.append("a minute after the pause unfilled")
for h in (" Reported               XYZF", " Sensor Orientation     XYZF", " Data Interval Type     Filtered 1-minute"):
    if not any(l.startswith(h) for l in day2): fails.append("the header lacks " + h)
both(p1, "second", start1, 1)

start4 = DAY0 + 86400 + 6 * 3600                                    # 4 Hz from 06:00 for 3 h
p4 = record("mag4", 4, start4, 3 * 3600)
both(p4, "minute", start4, 4)
both(p4, "second", start4, 4)
print("\n".join("FAIL " + x for x in fails) or "the twins the same, the field its own value: all good")
sys.exit(1 if fails else 0)
