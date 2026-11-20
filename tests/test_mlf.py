"""MLF's and NRC's cores: the C and the Python write the same sample file byte for byte
(write_mlf_sample.c), the Python reads it back — every segment's series decoded —, and reads it
again through the header's mirror with the header damaged, NRC round-trips
random series of every width with and without a reference, and damaged streams fail cleanly."""
import os
import random
import shlex
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import mlf  # noqa: E402
import nrc  # noqa: E402

N = 600
seed = 1


def lcg():
    global seed
    seed = (seed * 1103515245 + 12345) & 0xFFFFFFFF
    return (seed >> 8) & 0xFFFF


def series(seg):
    a, b, c, d, e = [], [], [], [], []
    walk = v = s = 0
    for _ in range(N):
        walk += lcg() % 21 - 10
        v += lcg() % 5 - 2
        s += v
        a.append(walk & 0xFFFF)
        b.append(s & 0xFFFFFFFF)
        c.append((walk + lcg() % 3 - 1) & 0xFFFF)
        d.append(seg & 0xFF)
        e.append(lcg() & 0xFFF)
    return a, b, c, d, e


SPEC = [(16, True, None), (32, True, None), (16, True, 0), (8, False, None), (12, False, None)]


def body(seg):
    cols = series(seg)
    w = nrc.BitWriter()
    w.gamma(N)
    for (width, signed, ref), xs in zip(SPEC, cols):
        nrc.write_series(w, xs, width, signed, cols[ref] if ref is not None else None)
    raw = b"".join(struct.pack("<I", x) for xs in cols for x in xs)
    return mlf.shortest([(mlf.RAW, raw), (mlf.NRC, w.bytes())]), cols


def decode(b):
    r = nrc.BitReader(b)
    n = r.gamma()
    cols = []
    for width, signed, ref in SPEC:
        cols.append(nrc.read_series(r, n, width, signed, cols[ref] if ref is not None else None))
    nrc.finish(r)
    return tuple(cols)


def python_file():
    w = mlf.Writer(mlf.Header(7, 1000000, 0x01020304, 0, [mlf.Section(1, b"profile")], letter="X"))
    expect = []
    for i in range(20):
        if i == 10:
            w.config(1000000 + 8 * i, [mlf.Section(2, b"new")])
        (codec, b), cols = body(i)
        expect.append(cols)
        assert w.segment(1000000 + 8 * i, 1000000 + 8 * i + 7, codec, b, 7 if i == 15 else None)   # 15 an even run
    return bytes(w.image), expect


def c_file():
    with tempfile.TemporaryDirectory() as t:
        exe, out = os.path.join(t, "w"), os.path.join(t, "f.mlf")
        subprocess.run(shlex.split(os.environ.get("CC", "cc")) + [
            "-std=c99", "-O2", "-Wall", "-Wextra", "-Wpedantic", "-Werror", "-ffp-contract=off",
            "-I" + os.path.join(ROOT, "mlf"), "-I" + os.path.join(ROOT, "nrc"), "-o", exe,
            os.path.join(HERE, "write_mlf_sample.c"), os.path.join(ROOT, "mlf", "mlf.c"),
            os.path.join(ROOT, "nrc", "nrc.c"), "-lm"], check=True)
        subprocess.run([exe, out], check=True)
        return open(out, "rb").read()


def main():
    image, expect = python_file()
    assert image == c_file(), "the C and the Python files differ"
    assert image[:4] == b"MLFX" and image[-512:] == image[:512], "the mark or the mirror"
    bad = bytearray(image)
    bad[5:9] = b"\xff\xff\xff\xff"                         # the header damaged
    r2 = mlf.Reader(bytes(bad))
    assert r2.header.letter == "X" and [o for _, o, _ in r2.objects()] == [o for _, o, _ in mlf.Reader(image).objects()]
    r = mlf.Reader(image)
    assert (r.header.source, r.header.sequence, r.header.letter) == (0x01020304, 7, "X")
    segs, nrcs, sections = 0, 0, None
    for t, obj, sections in r.objects():
        if t != mlf.SEG:
            continue
        cols = decode(obj["body"]) if obj["codec"] == mlf.NRC else tuple(
            list(struct.unpack_from("<%dI" % N, obj["body"], 4 * N * k)) for k in range(5))
        assert cols == expect[segs], "segment %d reads back other values" % segs
        assert (obj["run"], obj["place"]) == ((True, 7) if segs == 15 else (False, None)), "the time byte"
        nrcs += obj["codec"] == mlf.NRC
        segs += 1
    assert segs == 20 and [s.id for s in sections] == [1, 2]
    rnd = random.Random(5)
    for _ in range(300):
        w = rnd.choice([1, 7, 8, 12, 16, 24, 32])
        n = rnd.randint(0, 200)
        xs = [rnd.getrandbits(w) for _ in range(n)]
        if rnd.random() < 0.5:
            x = rnd.getrandbits(w)
            xs = [x := (x + rnd.randint(-3, 3)) & ((1 << w) - 1) for _ in range(n)]
        ref = [rnd.getrandbits(w) for _ in range(n)] if rnd.random() < 0.3 else None
        signed = rnd.random() < 0.5
        bw = nrc.BitWriter()
        nrc.write_series(bw, xs, w, signed, ref)
        b = bw.bytes()
        br = nrc.BitReader(b)
        assert nrc.read_series(br, n, w, signed, ref) == xs
        nrc.finish(br)
        if b:
            bad = bytearray(b)
            bad[rnd.randrange(len(bad))] ^= 1 << rnd.randrange(8)
            try:
                nrc.read_series(nrc.BitReader(bytes(bad)), n, w, signed, ref)
            except ValueError:
                pass
    print(f"20 segments ({nrcs} NRC, {20 - nrcs} raw) and a config: the C and the Python files "
          "the same byte for byte, every series read back, the mirror read with the header damaged; "
          "300 random series round-tripped")


if __name__ == "__main__":
    main()
