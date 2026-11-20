#!/usr/bin/env python3
"""NIC-SDK — MUF to miniSEED: python3 tools/muf2mseed.py FILE OUT.mseed NET STA LOC NAME[:k] [LAT LON ELEV]
— the twin of tools/muf2mseed.c, the same bytes.

One value of a stream, found by its group's name (and its place in the group, k, from 0), as
Steim-2 records; a pause in the steps or a missing value (a signed type's least, an unsigned one's
greatest) ends a run. Each sample's time from the head's start and the stream's rate. With the
station's place, OUT.mseed.xml beside it: StationXML, the sensitivity from the group's scale."""
import datetime, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import muf, mseed

MISSING = {"b": -128, "h": -32768, "i": -2**31, "B": 255, "H": 65535, "I": 2**32 - 1}


def export(path, out, net, sta, loc, which, place=None):
    f = muf.read(path)
    name, _, k = which.partition(":")
    k = int(k or 0)
    g = st = None
    for s in f["streams"]:
        for c in s["groups"]:
            if g is None and c["name"] == name:
                g, st = c, s
    if g is None or k >= g["count"] or g["type"] not in "bBhHiIqQ":
        raise SystemExit("no whole-number value %s:%d" % (name, k))
    if not st["rate_mhz"]:
        raise SystemExit("an irregular stream has no rate for miniSEED")
    off = 0
    for c in st["groups"]:
        if c is g:
            break
        off += c["count"]
    orient = str(1 + min(k, 8))
    if g["count"] == 1:
        orient = mseed.orientation(g["name"], orient)
    elif k < 3:
        orient = mseed.orientation("XYZ"[k], orient)
    cha = mseed.channel_code(st["rate_mhz"], 1000, g["unit"], orient)
    records = []
    w = mseed.Writer(net, sta, loc, cha, st["rate_mhz"], 1000, records.append)
    run, first, nxt = [], 0, 0
    for step, vals in f["steps"][st["kind"]]:
        v = vals[off + k]
        gone = v == MISSING.get(g["type"])
        if step != nxt or gone:
            if run:
                w.run(f["start_us"], first, run)
            run = []
        nxt = step + 1
        if gone:
            continue
        if not -2**31 <= v < 2**31:
            raise SystemExit("a value past 32 bits at step %d" % step)
        if not run:
            first = step
        run.append(v)
    if run:
        w.run(f["start_us"], first, run)
    open(out, "wb").write(b"".join(records))
    if place:
        lat, lon, elev = (float(x) for x in place)
        start = datetime.datetime.fromtimestamp(f["start_us"] // 1000000, datetime.timezone.utc)
        ch = dict(cha=cha, loc=loc, unit=g["unit"], rate_num=st["rate_mhz"], rate_den=1000,
                  counts_per_unit=1.0 / (g["mantissa"] * 10.0 ** g["exp10"]))
        xml = mseed.stationxml(net, sta, f["name"], start.strftime("%Y-%m-%dT%H:%M:%SZ"), lat, lon, elev, [ch])
        open(out + ".xml", "w", newline="\n").write(xml)


if __name__ == "__main__":
    if len(sys.argv) not in (7, 10):
        sys.exit(__doc__)
    export(*sys.argv[1:7], place=sys.argv[7:10] or None)
