#!/usr/bin/env python3
"""NIC-SDK — MLF to miniSEED: python3 tools/mlf2mseed.py GLUE.py OUT.mseed NET STA LOC NAME FILE…
— the twin of tools/mlf2mseed.c, the same bytes.

With the program's glue for its profile (tools/mlfseries.py says what a glue holds): one channel of
a series of files, by its name, as Steim-2 records, a gap ending a run, the quality bits as the glue
gives them; with the station known from the file, OUT.mseed.xml beside it."""
import datetime, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mlfseries, mseed


def export(glue, out, net, sta, loc, name, paths):
    ch, st, v = mlfseries.series(paths, glue, name)
    cha = mseed.channel_code(ch["rate_num"], ch["rate_den"], ch["unit"], mseed.orientation(ch["name"], "1"))
    records = []
    w = mseed.Writer(net, sta, loc, cha, ch["rate_num"], ch["rate_den"], records.append)
    i = 0
    while i < len(v):                                   # a run: numbers one apart
        j = i
        while j < len(v) and v[j][0] == v[i][0] + (j - i):
            j += 1
        w.run(0, v[i][0], [s[1] for s in v[i:j]], [s[2] for s in v[i:j]])
        i = j
    open(out, "wb").write(b"".join(records))
    if st and v:
        sec = v[0][0] * ch["rate_den"] // ch["rate_num"]
        start = datetime.datetime.fromtimestamp(sec, datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
        xc = dict(cha=cha, loc=loc, unit=ch["unit"], rate_num=ch["rate_num"], rate_den=ch["rate_den"],
                  counts_per_unit=1.0 / ((ch["mantissa"] or 1) * 10.0 ** ch["exp10"]))
        xml = mseed.stationxml(net, sta, st["name"], start, st["lat"], st["lon"], st["elev"], [xc])
        open(out + ".xml", "w", newline="\n").write(xml)


if __name__ == "__main__":
    if len(sys.argv) < 8:
        sys.exit(__doc__)
    export(mlfseries.load_glue(sys.argv[1]), *sys.argv[2:7], sys.argv[7:])
