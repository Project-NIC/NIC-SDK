#!/usr/bin/env python3
"""NIC-SDK — MUF to IAGA-2002: python3 tools/muf2iaga.py FILE DIR CODE LAT LON ELEV|- SOURCE [minute|second]
— the twin of tools/muf2iaga.c, the same bytes.

The magnetometer found by its description: the values in nT of the first stream that has them, up
to four — a group's letters its name's when they are as many as its values, else X Y Z F; each
sample's time from the head's start and the stream's rate, which must be whole hertz; a missing
value (a signed type's least, an unsigned one's greatest, a NaN) left out. One file a UTC day the
record touches, minute values unless `second` is asked, named as INTERMAGNET's:
DIR/<code><yyyymmdd>v<min|sec>.<min|sec>."""
import datetime, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import muf, iaga

MISSING = {"b": -128, "h": -32768, "i": -2**31, "B": 255, "H": 65535, "I": 2**32 - 1}


def export(path, outdir, code, lat, lon, elev, source, interval="minute"):
    minute = interval != "second"
    f = muf.read(path)
    comps, letters, st = [], "", None
    for s in f["streams"]:
        off = 0
        for g in s["groups"]:
            if g["unit"] == "nT" and (st is None or s is st):
                st = s
                for k in range(g["count"]):
                    if len(comps) == 4:
                        break
                    n = g["name"]
                    l = n[k] if len(n) == g["count"] else n[-1] if g["count"] == 1 and n else "XYZF"[len(comps)]
                    letters += l.upper()
                    comps.append((g, off + k))
            off += g["count"]
    if not comps:
        raise SystemExit("no values in nT")
    mhz = st["rate_mhz"]
    if not mhz or mhz % 1000:
        raise SystemExit("IAGA-2002 takes a whole number of samples a second")
    rate = mhz // 1000
    at = f["start_us"] * rate
    base = (at + 500000) // 1000000
    if at % 1000000:
        print("the start is not on a sample: moved to the nearest, %d µs" % int((base * 1000000 - at) / rate),
              file=sys.stderr)
    samples = []
    for step, vals in f["steps"][st["kind"]]:
        row = []
        for g, i in comps:
            v = vals[i]
            gone = v == MISSING.get(g["type"]) if g["type"] in MISSING else v != v
            row.append(float("nan") if gone else ((v + g["add"]) * g["mantissa"]) * 10.0 ** g["exp10"])
        samples.append((base + step, row))
    if not samples:
        raise SystemExit("no samples")
    station = dict(name=f["name"], code=code, source=source, lat=float(lat), lon=float(lon),
                   elev=None if elev == "-" else float(elev))
    written = []
    for day in range(samples[0][0] // (rate * 86400), samples[-1][0] // (rate * 86400) + 1):
        first, count = iaga.span(day, rate, minute)
        vals = [[iaga.NAN] * count for _ in comps]
        for num, row in samples:
            if first <= num < first + count:
                for c, v in enumerate(row):
                    vals[c][num - first] = v
        d = datetime.date(1970, 1, 1) + datetime.timedelta(days=day)
        ext = "min" if minute else "sec"
        out = os.path.join(outdir, "%s%sv%s.%s" % (code.lower()[:7], d.strftime("%Y%m%d"), ext, ext))
        open(out, "w", newline="\n").write(iaga.day_text(station, day, rate, minute, letters, vals))
        written.append(out)
    return written


if __name__ == "__main__":
    if len(sys.argv) not in (8, 9):
        sys.exit(__doc__)
    for p in export(*sys.argv[1:9]):
        print(p)
