#!/usr/bin/env python3
"""NIC-SDK — MUF's reader: python3 tools/muf.py FILE [--csv DIR]

Reads a MUF file as MUF.md sets it out: every block's CRC-32, the head and its continuations,
the streams and their groups, the set's table, the steps of every stream in order (by each
block's first step), the index blocks checked against what they list, the end. Values are
given raw and as physical = (raw + add) × mantissa × 10^exp10.
"""
import struct, sys, zlib, os, csv

BLOCK = 512
SIZES = {"b": 1, "B": 1, "h": 2, "H": 2, "i": 4, "I": 4, "q": 8, "Q": 8, "e": 2, "f": 4, "d": 8}


class MufError(Exception):
    pass


def read(path):
    data = open(path, "rb").read()
    if len(data) % BLOCK:
        raise MufError("not whole blocks")
    blocks = [data[i:i + BLOCK] for i in range(0, len(data), BLOCK)]
    good = []
    for n, b in enumerate(blocks):
        if zlib.crc32(b[:508]) != struct.unpack_from("<I", b, 508)[0]:
            good.append(None)          # damaged: skipped, the rest read
        else:
            good.append(b)
    h, heads = good[0], good
    if h is None or h[:3] != b"MUF" or h[5] != 0x01:
        h = None
        for k in range(1, min(256, len(good)) + 1):     # the head's mirror in the room's last blocks
            m = good[len(good) - k]
            if m is not None and m[:3] == b"MUF" and m[5] == 0x01 and m[22] == k and \
                    all(x is not None for x in good[len(good) - k:]):
                h, heads = m, good[len(good) - k:]
                break
        if h is None:
            raise MufError("no head")
    f = {"mark": h[:4].decode(), "format": h[4], "session": struct.unpack_from("<H", h, 6)[0],
         "unit": h[8], "chip": struct.unpack_from("<Q", h, 9)[0], "fw": tuple(h[17:20]),
         "time_source": h[23], "start_us": struct.unpack_from("<Q", h, 24)[0],
         "tz_q": struct.unpack_from("<b", h, 32)[0], "name": h[33:49].rstrip(b"\0").decode()}
    n_streams, n_groups, n_heads = h[20], h[21], h[22]
    streams = []
    for s in range(n_streams):
        kind, rate, spb, sb, fl = struct.unpack_from("<BIBBB", h, 64 + 8 * s)
        streams.append({"kind": kind, "rate_mhz": rate, "steps_per_block": spb, "step_bytes": sb,
                        "flags": fl, "groups": []})
    raw_groups = [h[208 + 36 * g:244 + 36 * g] for g in range(min(8, n_groups))]
    for c in range(1, n_heads):
        hb = heads[c]
        if hb is None or hb[5] != 0x02:
            raise MufError("a head's continuation missing")
        for k in range(13):
            if len(raw_groups) < n_groups:
                raw_groups.append(hb[12 + 36 * k:48 + 36 * k])
    by_kind = {s["kind"]: s for s in streams}
    for g in raw_groups:
        st, first, count, typ, gfl, e10, mant, add = struct.unpack_from("<BBBcBbhi", g, 0)
        grp = {"first": first, "count": count, "type": typ.decode(), "flags": gfl, "exp10": e10,
               "mantissa": mant or 1, "add": add, "name": g[12:20].rstrip(b"\0").decode(),
               "unit": g[20:28].rstrip(b"\0").decode(), "ref": g[28],
               "range": struct.unpack_from("<H", g, 29)[0]}
        by_kind[st]["groups"].append(grp)
    for s in streams:
        s["groups"].sort(key=lambda g: g["first"])
        layout = "".join(g["type"] * g["count"] for g in s["groups"])
        if s["groups"] and struct.calcsize("<" + layout) != s["step_bytes"]:
            raise MufError("stream 0x%02X: its groups make %d B, its line says %d" %
                           (s["kind"], struct.calcsize("<" + layout), s["step_bytes"]))
        s["layout"] = "<" + layout
    f["streams"] = streams

    steps = {s["kind"]: [] for s in streams}
    table, end, indexes, numbered = b"", None, [], []
    for i in range(n_heads, len(good)):
        b = good[i]
        if blocks[i] == bytes(BLOCK) or (b is not None and b[:3] == b"MUF"):
            continue                    # the room unwritten, or the head's mirror
        if b is None:
            numbered.append(None)
            continue
        kind, nsteps, session, number, first = struct.unpack_from("<BBHII", b, 0)
        numbered.append((number, kind, nsteps, first))
        if kind == 0x04:
            length = struct.unpack_from("<H", b, 12)[0]
            table += b[14:14 + min(494, length - len(table))]
        elif kind == 0x03:
            why, fast, nblocks, dropped = b[12], *struct.unpack_from("<III", b, 16)
            end = {"why": why, "fast_steps": fast, "blocks": nblocks, "dropped": dropped}
        elif kind == 0x07:
            first_listed = struct.unpack_from("<I", b, 12)[0]
            nexts = struct.unpack_from("<17I", b, 28)
            listed = [(b[96 + 2 * k], b[97 + 2 * k]) for k in range(200)]
            indexes.append((number, first_listed, nexts, listed))
        elif kind in by_kind:
            s = by_kind[kind]
            for k in range(nsteps):
                off = 28 + k * s["step_bytes"]
                steps[kind].append((first + k, b[off:off + s["step_bytes"]]))
    f["table"] = table.decode()
    f["end"] = end
    # the index: each lists the 200 (or fewer) numbered blocks before it, kind and steps
    by_number = {x[0]: x for x in numbered if x}
    slots = [s["kind"] for s in streams if s["kind"] != 0x06]   # the index's order, the constants left out
    for number, first_listed, nexts, listed in indexes:
        running = dict(zip(slots, nexts))
        for k, (kind, nsteps) in enumerate(listed):
            if kind == 0:
                break
            x = by_number.get(first_listed + k)
            if x and (x[1], x[2]) != (kind, nsteps):
                raise MufError("index %d disagrees at block %d" % (number, first_listed + k))
            if kind in running:
                # every block's first step known from the index alone — unless a pause jumped it
                if x and x[3] != running[kind]:
                    running[kind] = x[3]
                    f.setdefault("pauses", []).append((kind, first_listed + k))
                running[kind] += nsteps
    f["indexes"] = len(indexes)
    out = {}
    for s in streams:
        rows = sorted(steps[s["kind"]])
        out[s["kind"]] = [(n, struct.unpack(s["layout"], raw)) if s["groups"] else (n, raw) for n, raw in rows]
    f["steps"] = out
    return f


def physical(stream, values):
    """A step's values as physical numbers, group by group."""
    res, i = {}, 0
    for g in stream["groups"]:
        vals = values[i:i + g["count"]]
        i += g["count"]
        scale = g["mantissa"] * 10.0 ** g["exp10"]
        res[g["name"]] = [(v + g["add"]) * scale for v in vals] if g["count"] > 1 else (vals[0] + g["add"]) * scale
    return res


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    f = read(sys.argv[1])
    print("%s, session %d, unit %d, %d streams, %d index blocks, end %s" %
          (f["mark"], f["session"], f["unit"], len(f["streams"]), f["indexes"], f["end"]))
    if "--csv" in sys.argv:
        d = sys.argv[sys.argv.index("--csv") + 1]
        os.makedirs(d, exist_ok=True)
        for s in f["streams"]:
            if not s["groups"]:
                continue
            with open(os.path.join(d, "stream_%02X.csv" % s["kind"]), "w", newline="") as fh:
                w = csv.writer(fh)
                names = [g["name"] + ("_%d" % k if g["count"] > 1 else "") for g in s["groups"] for k in range(g["count"])]
                w.writerow(["step"] + names)
                for n, vals in f["steps"][s["kind"]]:
                    w.writerow([n] + list(vals))


if __name__ == "__main__":
    main()
