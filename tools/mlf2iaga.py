#!/usr/bin/env python3
"""NIC-SDK — MLF to IAGA-2002: python3 tools/mlf2iaga.py GLUE.py DIR CODE SOURCE minute|second NAME[,NAME…] FILE…
— the twin of tools/mlf2iaga.c, the same bytes.

With the program's glue for its profile: up to four channels by their names, each component's
letter its name's last; the station from the file; the rate whole hertz; a flagged sample left out.
One file a UTC day the series touches, named as INTERMAGNET's: DIR/<code><yyyymmdd>v<min|sec>.<min|sec>."""
import datetime, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mlfseries, iaga


def export(glue, outdir, code, source, interval, names, paths):
    minute = interval != "second"
    comps, letters, rate, st = [], "", None, None
    for n, name in enumerate(names.split(",")[:4]):
        ch, st, v = mlfseries.series(paths, glue, name)
        if not ch["rate_den"] or ch["rate_num"] % ch["rate_den"] or rate not in (None, ch["rate_num"] // ch["rate_den"]):
            raise SystemExit("IAGA-2002 takes one whole number of samples a second")
        rate = ch["rate_num"] // ch["rate_den"]
        letters += (ch["name"][-1:] or "XYZF"[n]).upper()
        comps.append((ch, v))
    if not st:
        raise SystemExit("no station in the file")
    idx = [s[0] for _, v in comps for s in v]
    if not idx:
        raise SystemExit("no samples")
    station = dict(name=st["name"], code=code, source=source, lat=st["lat"], lon=st["lon"], elev=st["elev"])
    written = []
    for day in range(min(idx) // (rate * 86400), max(idx) // (rate * 86400) + 1):
        first, count = iaga.span(day, rate, minute)
        vals = []
        for ch, v in comps:
            col = [iaga.NAN] * count
            for index, raw, q in v:
                if 0 <= index - first < count and not q:
                    col[index - first] = ((raw + ch["add"]) * (ch["mantissa"] or 1)) * 10.0 ** ch["exp10"]
            vals.append(col)
        d = datetime.date(1970, 1, 1) + datetime.timedelta(days=day)
        ext = "min" if minute else "sec"
        out = os.path.join(outdir, "%s%sv%s.%s" % (code.lower()[:7], d.strftime("%Y%m%d"), ext, ext))
        open(out, "w", newline="\n").write(iaga.day_text(station, day, rate, minute, letters, vals))
        written.append(out)
    return written


if __name__ == "__main__":
    if len(sys.argv) < 8:
        sys.exit(__doc__)
    for p in export(mlfseries.load_glue(sys.argv[1]), *sys.argv[2:7], sys.argv[7:]):
        print(p)
