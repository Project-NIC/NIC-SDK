#!/usr/bin/env python3
"""NIC-SDK — MUF to miniSEED, the twins held to each other: python3 tests/test_mseed.py
A seismometer's record written with the Python writer — three components at 100 Hz from a start
not on a whole second, a pause and a missing value among them, differences of every Steim-2 width
up to 30 bits — exported by the C and by the Python: the same records and the same StationXML
byte for byte; every record read back, its Steim-2 checked, every sample and its time where it
belongs. Where ObsPy is installed, it reads the records too."""
import os, struct, subprocess, sys, tempfile
here = os.path.dirname(os.path.abspath(__file__))
root = os.path.join(here, "..")
tools = os.path.join(root, "tools")
sys.path.insert(0, tools)
import mufwrite, muf2mseed, mseed

tmp = tempfile.mkdtemp()
path = os.path.join(tmp, "quake.muf")
out = open(path, "wb")
START = 1767225600123456                                            # 2026-01-01 00:00:00.123456 UTC
head = dict(letter="X", session=1, unit=1, chip=1, fw=(0, 5, 0), time_source=1,
            start_us=START, tz_q=0, name="Quake & co")
streams = [(0x10, 100000, 40, 12, 0)]                               # 100 Hz
groups = [dict(stream=0x10, first=0, count=3, type="i", exp10=-9, mantissa=5, name="VEL", unit="m/s")]
w = mufwrite.Writer(out.write, head, streams, groups)


def sample(n, axis):
    """quiet noise, then an event whose differences grow to 30 bits, back to quiet"""
    q = ((n * 2654435761 + axis * 97) % 41) - 20
    if 3000 <= n < 3400:
        return q + ((n - 3000) % 7 - 3) * (1 << min(26, 4 * ((n - 3000) // 50))) // 2
    return q + 1000 * axis


truth = {}                                                          # step → the three values
for n in range(6000):
    if n == 2000:
        w.skip(0, 250)                                              # a pause of 2,5 s
    step = n + (250 if n >= 2000 else 0)
    vals = [sample(n, a) for a in range(3)]
    if n == 4500:
        vals[1] = -2**31                                            # missing on Y
    truth[step] = vals
    w.step(0, struct.pack("<iii", *vals))
w.end(1)
out.close()

exe = os.path.join(tmp, "muf2mseed")
subprocess.check_call(["cc", "-std=c11", "-D_POSIX_C_SOURCE=200809L", "-Wall", "-Wextra", "-Werror", "-O2",
                       "-o", exe, os.path.join(tools, "muf2mseed.c"), os.path.join(root, "muf", "muf_read.c"),
                       os.path.join(root, "mseed", "mseed.c"), "-lm"])
fails = []
for k, cha in ((0, "HH1"), (1, "HH2"), (2, "HHZ")):
    c, p = os.path.join(tmp, "c%d.mseed" % k), os.path.join(tmp, "p%d.mseed" % k)
    subprocess.check_call([exe, path, c, "XX", "NIC", "00", "VEL:%d" % k, "50.1", "14.4", "250"])
    muf2mseed.export(path, p, "XX", "NIC", "00", "VEL:%d" % k, ("50.1", "14.4", "250"))
    cb, pb = open(c, "rb").read(), open(p, "rb").read()
    if cb != pb: fails.append("%s: the C's records are not the Python's" % cha)
    if open(c + ".xml").read() != open(p + ".xml").read(): fails.append("%s: the StationXMLs differ" % cha)
    recs = mseed.read(pb)
    got = {}
    for r in recs:
        if (r["net"], r["sta"], r["loc"], r["cha"], r["rate"]) != ("XX", "NIC", "00", cha, 100):
            fails.append("%s: a record's codes or rate: %r" % (cha, r)); break
        step0 = round((r["us"] - START) * 100 / 1e6)
        if r["us"] != START + (step0 * 10000):
            fails.append("%s: a record's time %d not on a step" % (cha, r["us"])); break
        for j, v in enumerate(r["samples"]):
            got[step0 + j] = v
    want = {s: v[k] for s, v in truth.items() if v[k] != -2**31}
    if got != want: fails.append("%s: the samples read back are not those written" % cha)
    runs = 1 + sum(1 for s in want if s - 1 not in want and s != min(want))
    if [r["seq"] for r in recs] != list(range(1, len(recs) + 1)): fails.append("%s: the sequence" % cha)
    print("%s: %d records, %d samples, %d runs" % (cha, len(recs), len(got), runs))
    try:
        import obspy
        st = obspy.read(p)
        n = sum(tr.stats.npts for tr in st)
        if n != len(want) or st[0].stats.starttime.timestamp * 1e6 - START > 1:
            fails.append("%s: ObsPy reads %d samples from %s" % (cha, n, st[0].stats.starttime))
        if k == 0: print("ObsPy reads the records: %d traces" % len(st))
    except ImportError:
        if k == 0: print("ObsPy not installed: its reading skipped")
xml = open(os.path.join(tmp, "p2.mseed.xml")).read()
for want in ('<Channel code="HHZ" locationCode="00" startDate="2026-01-01T00:00:00Z">',
             "<Value>200000000</Value>", "<Name>Quake &amp; co</Name>", "<SampleRate>100</SampleRate>"):
    if want not in xml: fails.append("the StationXML lacks " + want)
print("\n".join("FAIL " + x for x in fails) or "the twins the same, every sample read back: all good")
sys.exit(1 if fails else 0)
