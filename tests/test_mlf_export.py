#!/usr/bin/env python3
"""NIC-SDK — MLF to miniSEED and IAGA-2002, the twins held to each other: python3 tests/test_mlf_export.py
Two series of MLF files of the made-up profile X (tests/glue_example.*), written with the Python
core — a seismometer at 100 Hz over two files, a magnetometer at 1 Hz over midnight; each segment
raw or NRC, whichever is shorter, an even run where the records follow one another (its time
once, in the segment) and each record with its time where not; the file's seconds 18 s ahead of UTC; a gap, a clipped sample,
a late repair written in a later segment; the first file's header damaged, read through its
mirror — exported by the C, built with the C glue, and by the Python with the Python glue: the
same records, StationXML and IAGA-2002 byte for byte; the samples read back where they belong, the
repair's value in place of the first, the field its own value at every filled minute."""
import os, random, struct, subprocess, sys, tempfile
here = os.path.dirname(os.path.abspath(__file__))
root = os.path.join(here, "..")
tools = os.path.join(root, "tools")
sys.path.insert(0, tools)
sys.path.insert(0, here)
import mlf, mseed, mlf2mseed, mlf2iaga, glue_example as glue

tmp = tempfile.mkdtemp()
OFF = 18                                                            # the file's seconds less UTC
fails = []


def sections(rows, chans):
    b = struct.pack("<HB", rows, len(chans))
    for name, unit, e in chans:
        b += struct.pack("<8s8sbhi", name.encode(), unit.encode(), e, 1, 0)
    st = struct.pack("<32siih", "Test & Station".encode(), 491234567, 140123456, 496)
    return [mlf.Section(1, b), mlf.Section(2, st), mlf.Section(3, struct.pack("<b", OFF))]


def even(part, rows):
    """the records one after another on the grid: an even run"""
    return all((b[0] - a[0]) * rows + b[1] - a[1] == 1 for a, b in zip(part, part[1:]))


def runs(recs, rows, most):
    """the records cut into even runs of at most `most`, a gap closing one"""
    out, cur = [], []
    for r in recs:
        if cur and (len(cur) == most or not even([cur[-1], r], rows)):
            out.append(cur)
            cur = []
        cur.append(r)
    return out + [cur] if cur else out


def write(name, rows, chans, chunks):
    """Each chunk of records a segment — an even run where its records follow one another, else
    each with its time —, a file opened after another when full; the paths."""
    secs = sections(rows, chans)
    paths, seq, w = [], 0, None
    i = 0
    while i < len(chunks):
        part = chunks[i]
        if w is None:
            w = mlf.Writer(mlf.Header(seq, part[0][0], 1, 0, secs, letter="X"))
        run = len(part) > 1 and even(part, rows)
        raw = b"".join(struct.pack("<B%di" % len(chans), *r[2:]) if run else
                       struct.pack("<IBB%di" % len(chans), *r) for r in part)
        codec, body = mlf.shortest([(mlf.RAW, raw), (mlf.NRC, glue.nrc_body(part, len(chans), run))])
        if not w.segment(min(r[0] for r in part), max(r[0] for r in part), codec, body,
                         part[0][1] if run else None):
            paths.append(os.path.join(tmp, "%s%d.mlf" % (name, seq)))
            open(paths[-1], "wb").write(w.image)
            seq, w = seq + 1, None
            continue
        i += 1
    paths.append(os.path.join(tmp, "%s%d.mlf" % (name, seq)))
    open(paths[-1], "wb").write(w.image)
    return paths


def build(src):
    exe = os.path.join(tmp, os.path.basename(src)[:-2])
    subprocess.check_call(["cc", "-std=c11", "-D_POSIX_C_SOURCE=200809L", "-Wall", "-Wextra", "-Werror", "-O2",
                           "-ffp-contract=off", "-o", exe, src, os.path.join(tools, "mlfseries.c"),
                           os.path.join(root, "mlf", "mlf.c"), os.path.join(root, "nrc", "nrc.c"),
                           os.path.join(root, "mseed", "mseed.c"), os.path.join(root, "iaga", "iaga.c"),
                           os.path.join(here, "glue_example.c"), "-lm"])
    return exe


# ---------------------------------------------------------------- the seismometer: 100 Hz, two files
T0 = 1767225600 + OFF                                               # 2026-01-01 00:00:00 UTC, the file's scale
recs, truth = [], {}
rnd = random.Random(1)
for n in range(100 * 1800):                                         # 30 min
    sec, fr = T0 + n // 100, n % 100
    if 300 * 100 <= n < 310 * 100:
        continue                                                    # a gap of 10 s
    v = ((n * 2654435761) % 997) - 498 + (3000 * ((n // 7) % 5) if 50000 <= n < 50300 else 0)
    st = 6 if n == 40000 else 0
    noise = [rnd.getrandbits(24) - (1 << 23) for _ in (3, 5)]           # N and E, 24-bit noise
    recs.append((sec, fr, st, v, *noise))
    truth[(sec - OFF) * 100 + fr] = (v, 0x02 if st == 6 else 0)
late = (T0 + 100, 5, 0, 123456, 0, 0)                               # a repair of a frame written long before
truth[(late[0] - OFF) * 100 + late[1]] = (123456, 0)
spaths = write("quake", 100, [("VELZ", "m/s", -9), ("VELN", "m/s", -9), ("VELE", "m/s", -9)],
                runs(recs[:-1], 100, 800) + [[recs[-1], late]])   # the repair in the next segment
if len(spaths) < 2: fails.append("the seismometer's series fills one file only")

exe = build(os.path.join(tools, "mlf2mseed.c"))
c, p = os.path.join(tmp, "c.mseed"), os.path.join(tmp, "p.mseed")
subprocess.check_call([exe, c, "XX", "NIC", "00", "VELZ"] + spaths)
mlf2mseed.export(glue, p, "XX", "NIC", "00", "VELZ", spaths)
if open(c, "rb").read() != open(p, "rb").read(): fails.append("miniSEED: the C's records are not the Python's")
if open(c + ".xml").read() != open(p + ".xml").read(): fails.append("StationXML: the C's is not the Python's")
got = {}
for r in mseed.read(open(p, "rb").read()):
    if (r["cha"], r["rate"]) != ("HHZ", 100): fails.append("a record's channel or rate: %r" % ((r["cha"], r["rate"]),)); break
    first = round(r["us"] * 100 / 1e6)
    if r["us"] != first * 10000: fails.append("a record's time off the grid"); break
    for j, v in enumerate(r["samples"]):
        got[first + j] = (v, r["quality"])
if {k: v[0] for k, v in got.items()} != {k: v[0] for k, v in truth.items()}:
    fails.append("miniSEED: the samples read back are not those written, the repair in place")
if not any(q & 0x02 for v, q in got.values()): fails.append("the clipped sample's record lacks its bit")
xml = open(p + ".xml").read()
for want in ('startDate="2026-01-01T00:00:00Z"', "<Latitude>49.1234567</Latitude>", "Test &amp; Station"):
    if want not in xml: fails.append("the StationXML lacks " + want)
print("miniSEED: %d files, %d samples read back, the repair and the gap in place" % (len(spaths), len(got)))

# ---------------------------------------------------------------- the magnetometer: 1 Hz over midnight
M0 = 1767225600 + 22 * 3600 + 1800 + OFF                            # 22:30 UTC
recs = [(M0 + n, 0, 0, 2000000 + n, -150000 + (n * 7919) % 301, 4400000 + (n * 104729) % 1001)
        for n in range(3 * 3600)]
mpaths = write("gauss", 1, [("MAGX", "nT", -2), ("MAGY", "nT", -2), ("MAGZ", "nT", -2)],
               runs(recs[:-1], 1, 600) + [[recs[-1]]])  # the last alone: raw
segs = [o for q in spaths + mpaths for t, o, _ in mlf.Reader(open(q, "rb").read()).objects() if t == mlf.SEG]
if {o["codec"] for o in segs} != {mlf.RAW, mlf.NRC}: fails.append("the segments not both raw and NRC")
if {o["run"] for o in segs} != {True, False}: fails.append("the segments not both even runs and timed records")
bad = bytearray(open(mpaths[0], "rb").read())
bad[8:12] = b"\xff\xff\xff\xff"                                     # the header damaged: the mirror reads
open(mpaths[0], "wb").write(bad)
exe = build(os.path.join(tools, "mlf2iaga.c"))
for interval in ("minute", "second"):
    cd, pd = os.path.join(tmp, "c" + interval), os.path.join(tmp, "p" + interval)
    os.makedirs(cd), os.makedirs(pd)
    args = ["BDV", "NIC-SDK test", interval, "MAGX,MAGY,MAGZ"]
    cout = subprocess.check_output([exe, cd] + args + mpaths).decode().split()
    pout = mlf2iaga.export(glue, pd, *args, mpaths)
    names = sorted(os.path.basename(x) for x in pout)
    if names != sorted(os.path.basename(x) for x in cout): fails.append("IAGA: the files' names differ")
    for n in names:
        if open(os.path.join(cd, n)).read() != open(os.path.join(pd, n)).read():
            fails.append("IAGA %s: the C's is not the Python's" % n)
        rows = [l for l in open(os.path.join(pd, n)).read().splitlines() if l[:2] == "20"]
        filled = 0
        for l in rows:
            s = 1767225600 + (int(l[8:10]) - 1) * 86400 + int(l[11:13]) * 3600 + int(l[14:16]) * 60 + int(l[17:19])
            x = float(l[30:40])
            if x == 99999.0:
                continue
            filled += 1
            if abs(x - (20000 + 0.01 * (s - (M0 - OFF)))) > 0.006:
                fails.append("IAGA %s %s: X %.2f" % (n, l[:19], x)); break
        print("IAGA %s: %d rows, %d filled" % (n, len(rows), filled))
    if interval == "minute" and names != ["bdv20260101vmin.min", "bdv20260102vmin.min"]:
        fails.append("IAGA: the days %r" % names)
print("\n".join("FAIL " + x for x in fails) or "the twins the same through the glue, the mirror read: all good")
sys.exit(1 if fails else 0)
