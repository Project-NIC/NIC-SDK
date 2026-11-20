"""An example of a program's glue for tools/mlfseries.py — a made-up profile of letter X, for the
tests only — an example, not a profile of the SDK; each program writes its own glue and keeps it
in its own repository. Its twin: glue_example.c.

The profile X: section 1 the channels — rows u16 (records a second), n u8, then per channel name 8 B,
unit 8 B, exp10 i8, mantissa i16, add i32; section 2 the station — name 32 B, latitude and longitude
i32 in 10⁻⁷ degree, elevation i16 m; section 3 the time — the file's seconds less UTC, i8. A record:
second u32 · frame u8 · status u8 · n values i32; in an even run (MLF.md §4) status u8 · n values,
its time the segment's. Status 6 clipped, 7 disturbed. Codec 0 the records back to back; codec 1
NRC: γ(count), the seconds (32 bits) and the frames (8) but in an even run, the statuses (8), then
each channel (32, signed)."""
import os, struct, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools"))
import mlf, nrc

LETTER = "X"
CHANNELS, STATION, TIME = 1, 2, 3
QUALITY = {6: 0x02, 7: 0x08}


def _body(sections, sid):
    return next((s.body for s in sections if s.id == sid), None)


def _channels(sections):
    b = _body(sections, CHANNELS)
    if b is None:
        return 0, []
    rows, n = struct.unpack_from("<HB", b, 0)
    out = []
    for k in range(n):
        name, unit, e, m, add = struct.unpack_from("<8s8sbhi", b, 3 + 23 * k)
        out.append(dict(name=name.rstrip(b"\0").decode(), unit=unit.rstrip(b"\0").decode(), exp10=e,
                        mantissa=m, add=add, rate_num=rows, rate_den=1))
    return rows, out


def channel(sections, name):
    return next((c for c in _channels(sections)[1] if c["name"] == name), None)


def station(sections):
    b = _body(sections, STATION)
    if b is None:
        return None
    name, lat, lon, elev = struct.unpack_from("<32siih", b)
    return dict(name=name.rstrip(b"\0").decode(), lat=lat / 1e7, lon=lon / 1e7, elev=float(elev))


def records(sections, seg):
    """(second, frame, status, values…) a record, its time from the segment in an even run."""
    rows, chans = _channels(sections)
    n, body, run = len(chans), seg["body"], seg["run"]
    if seg["codec"] == mlf.RAW:
        size = (1 if run else 6) + 4 * n
        if len(body) % size:
            raise ValueError("X: a record cut short")
        recs = [struct.unpack_from(("<B%di" if run else "<IBB%di") % n, body, i) for i in range(0, len(body), size)]
    elif seg["codec"] == mlf.NRC:
        r = nrc.BitReader(body)
        count = r.gamma()
        cols = [] if run else [nrc.read_series(r, count, 32), nrc.read_series(r, count, 8)]
        cols.append(nrc.read_series(r, count, 8))
        cols += [[v - (1 << 32) if v >> 31 else v for v in nrc.read_series(r, count, 32, True)] for _ in range(n)]
        nrc.finish(r)
        recs = list(zip(*cols))
    else:
        raise ValueError("X: a codec unknown")
    if run:                     # the time once, in the segment: the first second and the place
        at = seg["place"]
        recs = [(seg["first"] + (at + i) // rows, (at + i) % rows) + tuple(rec) for i, rec in enumerate(recs)]
    return recs


def samples(sections, seg, name):
    rows, chans = _channels(sections)
    k = next(i for i, c in enumerate(chans) if c["name"] == name)
    t = _body(sections, TIME)
    off = struct.unpack_from("<b", t)[0] if t else 0
    for rec in records(sections, seg):
        yield (rec[0] - off) * rows + rec[1], rec[3 + k], QUALITY.get(rec[2], 0)


def nrc_body(recs, n, run=False):
    """For the tests' writer: the records as profile X codes them with NRC."""
    w = nrc.BitWriter()
    w.gamma(len(recs))
    cols = list(zip(*recs))
    if not run:
        nrc.write_series(w, cols[0], 32)
        nrc.write_series(w, cols[1], 8)
    nrc.write_series(w, cols[2], 8)
    for k in range(n):
        nrc.write_series(w, [v & 0xFFFFFFFF for v in cols[3 + k]], 32, True)
    return w.bytes()
