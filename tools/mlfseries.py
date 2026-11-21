"""NIC-SDK — one channel's series out of a series of MLF files, through the program's glue; the twin
of tools/mlfseries.c.

MLF's core does not say what a record is — the profile does, named by the mark's letter — so the
SDK walks the files, the configs and the sections in force, and the program's glue reads the
segments. A glue is a Python module (an example: tests/glue_example.py) with:

  LETTER                              the profile it reads
  channel(sections, name)             a dict — name, unit, exp10, mantissa, add, rate_num, rate_den,
                                      and, if the glue knows it, orient (SEED's orientation letter;
                                      otherwise the name's last letter says) — or None
  station(sections)                   a dict — name, lat, lon, elev — or None
  samples(sections, segment, name)    (index, raw, quality) from a segment as mlf.py parses it —
                                      its first second, its time (`run`, `place`), codec and body:
                                      the number t · rate since 1970 UTC,
                                      the raw value, SEED's data-quality bits (0x02 clipped, 0x08
                                      glitches; 0 clean)
"""
import importlib.util, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mlf


def load_glue(path):
    spec = importlib.util.spec_from_file_location("glue", path)
    glue = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(glue)
    return glue


def series(paths, glue, name):
    """The files in order: (the channel, the station, the samples sorted by number as (index, raw,
    quality)); of two samples for one index the later written is kept."""
    kept, ch, st = {}, None, None
    for p in paths:
        r = mlf.Reader(open(p, "rb").read())
        if r.header.letter != glue.LETTER:
            raise SystemExit("%s: not an MLF file of the profile %s" % (p, glue.LETTER))
        for t, obj, sections in r.objects():
            if t != mlf.SEG:
                continue
            if ch is None:
                ch = glue.channel(sections, name)
                if ch is None:
                    continue
                st = glue.station(sections)
            try:
                got = list(glue.samples(sections, obj, name))
            except ValueError:
                continue                        # a segment not read costs itself
            for index, raw, q in got:
                kept.pop(index, None)
                kept[index] = (raw, q)
    if ch is None:
        raise SystemExit("no channel %s" % name)
    return ch, st, [(i, raw, q) for i, (raw, q) in sorted(kept.items())]
