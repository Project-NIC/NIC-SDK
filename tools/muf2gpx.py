#!/usr/bin/env python3
"""NIC-SDK — MUF to GPX: python3 tools/muf2gpx.py FILE OUT.gpx — the twin of tools/muf2gpx.c.

The position is found by its description, not by a name: the first group of two values in degrees
(latitude, longitude), and in the same stream a group of one value in metres (the height). Each
step's time from the head's start and the stream's rate; a step whose position is missing (a
signed type's least) left out."""
import sys, os, datetime
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import muf

MISSING = {"b": -128, "h": -32768, "i": -2**31, "q": -2**63}


def fixed(raw, exp10):
    """An integer raw value × 10^exp10 written exactly, as the C writes it."""
    neg, a = raw < 0, abs(raw)
    if exp10 >= 0:
        return ("-" if neg else "") + str(a * 10 ** exp10)
    d = -exp10
    return ("-" if neg else "") + "%d.%0*d" % (a // 10 ** d, d, a % 10 ** d)


def gpx(path):
    f = muf.read(path)
    ll = h = st = None
    for s in f["streams"]:
        for g in s["groups"]:
            if ll is None and g["count"] == 2 and g["unit"] == "deg":
                ll, st = g, s
    if ll is None:
        raise SystemExit("no position (a group of two values in degrees)")
    for g in st["groups"]:
        if g["count"] == 1 and g["unit"] == "m":
            h = g
            break
    offs, i = {}, 0
    for g in st["groups"]:
        offs[id(g)] = i
        i += g["count"]
    out = ['<?xml version="1.0" encoding="UTF-8"?>',
           '<gpx version="1.1" creator="NIC-SDK muf2gpx" xmlns="http://www.topografix.com/GPX/1/1">',
           "<trk><name>%s</name><trkseg>" % f["name"]]
    for step, vals in f["steps"][st["kind"]]:
        lat, lon = vals[offs[id(ll)]], vals[offs[id(ll)] + 1]
        if lat == MISSING.get(ll["type"]) or lon == MISSING.get(ll["type"]):
            continue
        line = '<trkpt lat="%s" lon="%s">' % (fixed((lat + ll["add"]) * ll["mantissa"], ll["exp10"]),
                                               fixed((lon + ll["add"]) * ll["mantissa"], ll["exp10"]))
        if h is not None:
            v = vals[offs[id(h)]]
            if v != MISSING.get(h["type"]):
                line += "<ele>%s</ele>" % fixed((v + h["add"]) * h["mantissa"], h["exp10"])
        if f["start_us"] and st["rate_mhz"]:
            us = f["start_us"] + step * 1000000000 // st["rate_mhz"]
            t = datetime.datetime.fromtimestamp(us // 1000000, datetime.timezone.utc)
            line += "<time>%s.%03dZ</time>" % (t.strftime("%Y-%m-%dT%H:%M:%S"), us % 1000000 // 1000)
        out.append(line + "</trkpt>")
    out += ["</trkseg></trk>", "</gpx>", ""]
    return "\n".join(out)


if __name__ == "__main__":
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    open(sys.argv[2], "w", newline="\n").write(gpx(sys.argv[1]))
