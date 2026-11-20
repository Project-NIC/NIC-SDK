#!/usr/bin/env python3
"""NIC-SDK — IAGA-2002 in Python, the twin of iaga/iaga.c, the same bytes: a magnetometer's day
as INTERMAGNET's exchange text, minute values by INTERMAGNET's Gaussian or second values, faster
samples first brought to 1 Hz by a low-pass. Bound to no container: a caller hands it each
component's samples in nT on the day's span. The floating point in the C's order, step by step
(no sum(), which since Python 3.12 adds otherwise)."""
import datetime, math

GAUSS_TAPS, GAUSS_SIGMA = 91, 15.8734
NAN = float("nan")


def gaussian():
    w, s = [], 0.0
    for n in range(GAUSS_TAPS):
        d = (n - 45.0) / GAUSS_SIGMA
        w.append(math.exp(-0.5 * (d * d)))
        s += w[-1]
    return [x / s for x in w]


def lowpass(rate):
    """From rate Hz to 1 Hz: a windowed sinc at 0.25 Hz, Blackman, 2·⌊123·rate/20⌋ + 1 taps."""
    m = 123 * rate // 20
    taps, fc, s, w = 2 * m + 1, 2 * 0.25 / rate, 0.0, []
    for n in range(taps):
        x = float(n - m)
        sinc = 1.0 if x == 0 else math.sin(math.pi * fc * x) / (math.pi * fc * x)
        win = 0.42 - 0.5 * math.cos(2 * math.pi * n / (taps - 1)) + 0.08 * math.cos(4 * math.pi * n / (taps - 1))
        w.append(fc * sinc * win)
        s += w[-1]
    return [x / s for x in w]


def _seconds(day, minute):
    return day * 86400 - (45 if minute else 0), (day + 1) * 86400 - (15 if minute else 1)


def span(day, rate, minute):
    """The samples a day needs, by their number t·rate since 1970: (the first, how many)."""
    s0, s1 = _seconds(day, minute)
    m = 123 * rate // 20 if rate > 1 else 0
    return s0 * rate - m, (s1 - s0) * rate + 1 + 2 * m


def _filt(w, values, first, centre):
    m = (len(w) - 1) // 2
    acc = wsum = 0.0
    have = 0
    for j, wj in enumerate(w):
        i = centre + j - m - first
        if 0 <= i < len(values) and values[i] == values[i]:
            acc += wj * values[i]
            wsum += wj
            have += 1
    return acc / wsum if 10 * have >= 9 * len(w) and wsum != 0 else NAN


def day_text(station, day, rate, minute, letters, vals):
    """One station's day: station a dict of name, code, source, lat, lon, elev (None if not known);
    vals each component's samples from the span's first, NaN where none."""
    s0, s1 = _seconds(day, minute)
    first, _ = span(day, rate, minute)
    g, lp = gaussian(), lowpass(rate) if rate > 1 else None
    cols, names = [], list(letters[:4])
    for c in range(len(names)):
        if rate == 1:
            sec = list(vals[c][:s1 - s0 + 1])
        else:
            sec = [_filt(lp, vals[c], first, s * rate) for s in range(s0, s1 + 1)]
        cols.append([_filt(g, sec, s0, day * 86400 + i * 60) for i in range(1440)] if minute else sec)
    given = "".join(names)
    while len(names) < 4:
        names.append("G" if "F" in names else "F")
    head = lambda label, value: " %-23s%-45s|" % (label, value[:45])
    lon = math.fmod(station["lon"], 360.0)
    lines = [head("Format", "IAGA-2002"), head("Source of Data", station["source"]),
             head("Station Name", station["name"]), head("IAGA Code", station["code"]),
             head("Geodetic Latitude", "%.3f" % station["lat"]),
             head("Geodetic Longitude", "%07.3f" % (lon + 360.0 if lon < 0 else lon)),
             head("Elevation", "" if station.get("elev") is None else "%g" % station["elev"]),
             head("Reported", "".join(names)), head("Sensor Orientation", given),
             head("Digital Sampling", "1 second" if rate == 1 else "%g second" % (1.0 / rate)),
             head("Data Interval Type", "Filtered 1-minute (00:15-01:45)" if minute else
                  "1-second" if rate == 1 else "Filtered 1-second"),
             head("Data Type", "variation")]
    if minute:
        lines.append(head("# Minute values", "Gaussian, 91 taps, sigma 15.8734 s, centred"))
    if rate != 1:
        lines.append(head("# Second values", "low-pass 0.25 Hz, %d taps, Blackman" % len(lp)))
    c4 = [station["code"] + n for n in names]
    lines.append("DATE       TIME         DOY     %-10s%-10s%-10s%-7s|" % tuple(c4))
    d = datetime.date(1970, 1, 1) + datetime.timedelta(days=day)
    for i in range(1440 if minute else 86400):
        s = i * (60 if minute else 1)
        v = [88888.0 if c >= len(cols) else 99999.0 if cols[c][i] != cols[c][i] else cols[c][i] for c in range(4)]
        lines.append("%04d-%02d-%02d %02d:%02d:%02d.000 %03d   %10.2f%10.2f%10.2f%10.2f"
                     % (d.year, d.month, d.day, s // 3600, s // 60 % 60, s % 60, d.timetuple().tm_yday, *v))
    return "\n".join(lines) + "\n"
