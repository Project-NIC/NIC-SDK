#!/usr/bin/env python3
"""NIC-SDK — miniSEED 2.4 in Python, the twin of mseed/mseed.c, the same bytes: integer samples
as Steim-2 in 512 B records, FDSN StationXML 1.2 beside them, and a small reader of the records
for the tests. Bound to no container: a caller hands it runs of samples one period apart.

python3 tools/mseed.py FILE.mseed — the records listed and their samples checked."""
import datetime, struct, sys
from math import gcd

RECORD, FRAMES = 512, 7
CLIPPED, GLITCHES = 0x02, 0x08
FORMS = [(7, 4, 3, 2), (6, 5, 3, 1), (5, 6, 3, 0), (4, 8, 1, -1), (3, 10, 2, 3), (2, 15, 2, 2), (1, 30, 2, 1)]


class MseedError(Exception):
    pass


def _fits(d, bits):
    return -(1 << (bits - 1)) <= d < (1 << (bits - 1))


def steim2(x, prev=None):
    """As many samples as 7 frames take: (the frames' 448 bytes, the count)."""
    n = len(x)
    diff = lambda j: x[j] - x[j - 1] if j else (x[0] - prev if prev is not None else 0)
    frames, i = [], 0
    for f in range(FRAMES):
        if i >= n:
            break
        words, nibs = [0], [0]
        if f == 0:
            words += [x[0] & 0xFFFFFFFF, 0]
            nibs += [0, 0]
        while len(words) < 16 and i < n:
            for count, bits, nib, dnib in FORMS:
                if count <= n - i and all(_fits(diff(i + k), bits) for k in range(count)):
                    break
            else:
                raise MseedError("a difference past Steim-2's 30 bits")
            body = 0
            for k in range(count):
                body = (body << bits) | (diff(i + k) & ((1 << bits) - 1))
            words.append((body if dnib < 0 else (dnib << 30) | body) & 0xFFFFFFFF)
            nibs.append(nib)
            i += count
        nibs += [0] * (16 - len(nibs))
        words += [0] * (16 - len(words))
        w0 = 0
        for nb in nibs:
            w0 = (w0 << 2) | nb
        words[0] = w0
        frames.append(words)
    frames[0][2] = x[i - 1] & 0xFFFFFFFF
    data = b"".join(w.to_bytes(4, "big") for ws in frames for w in ws)
    return data + bytes(FRAMES * 64 - len(data)), i


def _sext(v, bits):
    v &= (1 << bits) - 1
    return v - (1 << bits) if v >> (bits - 1) else v


def unsteim2(frames, count):
    """Back from the frames; the integration check against frame 0's last sample."""
    out, acc = [], 0
    x0, xn = struct.unpack_from(">ii", frames, 4)
    for f in range(len(frames) // 64):
        w0 = struct.unpack_from(">I", frames, 64 * f)[0]
        for word in range(3 if f == 0 else 1, 16):
            if len(out) >= count:
                break
            v = struct.unpack_from(">I", frames, 64 * f + 4 * word)[0]
            nib, d = (w0 >> (2 * (15 - word))) & 3, v >> 30
            cnt, bits = {1: (4, 8), 2: {1: (1, 30), 2: (2, 15), 3: (3, 10)}.get(d, (0, 0)),
                         3: {0: (5, 6), 1: (6, 5), 2: (7, 4)}.get(d, (0, 0))}.get(nib, (0, 0))
            for k in range(cnt):
                if len(out) >= count:
                    break
                acc = x0 if not out else ((acc + _sext(v >> (bits * (cnt - 1 - k)), bits) + 2**31) % 2**32) - 2**31
                out.append(acc)
    if len(out) != count or not count or out[-1] != xn:
        raise MseedError("Steim-2's integration check failed")
    return out


def _civil(days):
    d = datetime.date(1970, 1, 1) + datetime.timedelta(days=days)
    return d.year, d.timetuple().tm_yday


class Writer:
    def __init__(self, net, sta, loc, cha, rate_num, rate_den, write):
        pad = lambda s, n: (s or "").encode("ascii")[:n].ljust(n)
        self.net, self.sta, self.loc, self.cha = pad(net, 2), pad(sta, 5), pad(loc, 2), pad(cha, 3)
        if not rate_num or not rate_den:
            raise MseedError("no rate")
        g = gcd(rate_num, rate_den)
        self.num, self.den = rate_num // g, rate_den // g
        if self.den == 1 and self.num <= 32767:
            self.factor, self.mult = self.num, 1
        elif self.num == 1 and self.den <= 32767:
            self.factor, self.mult = -self.den, 1
        elif self.num <= 32767 and self.den <= 32767:
            self.factor, self.mult = self.num, -self.den
        else:
            raise MseedError("the rate not written in SEED's two 16-bit numbers")
        self.seq, self.write = 1, write

    def at(self, base_us, index):
        """base + index / rate, in µs, rounded a half up"""
        return base_us + (index * 1000000 * self.den * 2 + self.num) // (2 * self.num)

    def run(self, base_us, first, x, quality=None):
        """n samples one period apart, the k-th at base_us + (first + k) / rate."""
        k = 0
        while k < len(x):
            data, c = steim2(x[k:], x[k - 1] if k else None)
            q = 0
            for j in range(c if quality else 0):
                q |= quality[k + j] & (CLIPPED | GLITCHES)
            units, rem = divmod(self.at(base_us, first + k), 100)
            if rem >= 50:
                units, rem = units + 1, rem - 100
            secs, tenths = divmod(units, 10000)
            days, s = divmod(secs, 86400)
            year, doy = _civil(days)
            used = sum(1 for f in range(FRAMES) if f == 0 or data[64 * f:64 * f + 4] != b"\0\0\0\0")
            head = (b"%06dD " % self.seq + self.sta + self.loc + self.cha + self.net
                    + struct.pack(">HHBBBBHHhhBBBBiHH", year, doy, s // 3600, s // 60 % 60, s % 60, 0, tenths,
                                  c, self.factor, self.mult, 0, 0, q, 2, 0, 64, 48))
            b1000 = struct.pack(">HHBBBB", 1000, 56, 11, 1, 9, 0)
            b1001 = struct.pack(">HHBbBB", 1001, 0, 100, rem, 0, used)
            self.write(head + b1000 + b1001 + data)
            self.seq = 1 if self.seq == 999999 else self.seq + 1
            k += c


def channel_code(rate_num, rate_den, unit, orient):
    """The band by the rate, the instrument by the unit, the orientation given."""
    r = rate_num / rate_den
    band = ("F" if r >= 1000 else "C" if r >= 250 else "H" if r >= 80 else "B" if r >= 10 else
            "M" if r > 1 else "L" if r >= 0.5 else "V" if r >= 0.05 else "U" if r >= 0.005 else "R")
    u = "".join(c for c in (unit or "") if c not in " ^*")[:15].lower()
    inst = ("N" if u in ("m/s2", "m/s/s") else "H" if u == "m/s" else "F" if u in ("nt", "t") else
            "D" if u in ("pa", "hpa", "kpa", "bar", "mbar") else "K" if u in ("k", "degc", "c") else "Y")
    return band + inst + orient


def orientation(name, fallback):
    """Z N E as they are, X 1, Y 2, by a name's last letter; otherwise the fallback."""
    c = (name or "").rstrip(" ")[-1:].upper()
    return c if c in ("Z", "N", "E") else "1" if c == "X" else "2" if c == "Y" else fallback


def _esc(s):
    return (s or "").replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def stationxml(net, sta, site, start, lat, lon, elev, channels):
    """One station's document; channels: dicts of cha, loc, unit, rate_num, rate_den,
    counts_per_unit."""
    o = ['<?xml version="1.0" encoding="UTF-8"?>\n'
         '<FDSNStationXML xmlns="http://www.fdsn.org/xml/station/1" schemaVersion="1.2">\n'
         ' <Source>NIC-SDK</Source>\n <Created>%s</Created>\n <Network code="%s">\n'
         '  <Station code="%s" startDate="%s">\n' % (_esc(start), _esc(net), _esc(sta), _esc(start)),
         "   <Latitude>%.7f</Latitude>\n   <Longitude>%.7f</Longitude>\n   <Elevation>%g</Elevation>\n"
         % (lat, lon, elev),
         "   <Site><Name>%s</Name></Site>\n" % _esc(site)]
    for c in channels:
        o.append('   <Channel code="%s" locationCode="%s" startDate="%s">\n' % (_esc(c["cha"]), _esc(c["loc"]), _esc(start))
                 + "    <Latitude>%.7f</Latitude>\n    <Longitude>%.7f</Longitude>\n    <Elevation>%g</Elevation>\n"
                 % (lat, lon, elev)
                 + "    <Depth>0</Depth>\n    <SampleRate>%g</SampleRate>\n" % (c["rate_num"] / c["rate_den"])
                 + "    <Response>\n     <InstrumentSensitivity>\n      <Value>%.10g</Value>\n" % c["counts_per_unit"]
                 + "      <Frequency>1</Frequency>\n      <InputUnits><Name>%s</Name></InputUnits>\n" % _esc(c["unit"] or "count")
                 + "      <OutputUnits><Name>count</Name></OutputUnits>\n     </InstrumentSensitivity>\n"
                 "    </Response>\n   </Channel>\n")
    o.append("  </Station>\n </Network>\n</FDSNStationXML>\n")
    return "".join(o)


def read(data):
    """The records: dicts of the codes, the start in µs, the rate, the quality bits and the
    samples, each record's Steim-2 checked."""
    out = []
    for p in range(0, len(data), RECORD):
        r = data[p:p + RECORD]
        sta, loc, cha, net = r[8:13], r[13:15], r[15:18], r[18:20]
        year, doy, hh, mm, ss, _, tenths, count, fac, mult, _, _, q, nb, _, begin, first = \
            struct.unpack_from(">HHBBBBHHhhBBBBiHH", r, 20)
        us = struct.unpack_from(">b", r, 61)[0] if struct.unpack_from(">H", r, 56)[0] == 1001 else 0
        if struct.unpack_from(">HHBBB", r, 48)[:5] != (1000, 56, 11, 1, 9):
            raise MseedError("not Steim-2 in 512 B big-endian")
        days = (datetime.date(year, 1, 1) - datetime.date(1970, 1, 1)).days + doy - 1
        t = ((days * 86400 + hh * 3600 + mm * 60 + ss) * 10000 + tenths) * 100 + us
        rate = (fac if fac > 0 else -1 / fac) * (mult if mult > 0 else -1 / mult)
        out.append(dict(seq=int(r[:6]), sta=sta.decode().strip(), loc=loc.decode().strip(), cha=cha.decode().strip(),
                        net=net.decode().strip(), us=t, rate=rate, quality=q,
                        samples=unsteim2(r[begin:], count)))
    return out


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    recs = read(open(sys.argv[1], "rb").read())
    n = sum(len(r["samples"]) for r in recs)
    for r in recs[:3]:
        print(r["seq"], r["net"], r["sta"], r["loc"], r["cha"], r["us"], r["rate"], len(r["samples"]))
    print("%d records, %d samples, every record's Steim-2 checked" % (len(recs), n))


if __name__ == "__main__":
    main()
