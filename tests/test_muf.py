#!/usr/bin/env python3
"""NIC-SDK — MUF's C writer read back by tools/muf.py: python3 tests/test_muf.py"""
import os, subprocess, sys, tempfile
here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(here, "..", "tools"))
import muf, mufwrite, struct, filecmp

tmp = tempfile.mkdtemp()
exe, path = os.path.join(tmp, "w"), os.path.join(tmp, "sample.muf")
subprocess.check_call(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-O2", "-o", exe,
                       os.path.join(here, "write_sample.c"), os.path.join(here, "..", "muf", "muf.c")])
subprocess.check_call([exe, path])
f = muf.read(path)
fails = 0
def check(c, what):
    global fails
    if not c:
        fails += 1
        print("FAIL", what)

check(f["mark"] == "MUFX" and f["session"] == 7 and f["unit"] == 3 and f["name"] == "sample", "the head")
check(len(f["streams"]) == 5 and sum(len(s["groups"]) for s in f["streams"]) == 18, "18 groups, 8 + 10")
fast = f["steps"][0x10]
check([n for n, _ in fast] == list(range(60000)), "the fast stream whole, in order")
check(all(v[0] == (n % 2000) - 1000 and v[12] == 101325 * 64 - n for n, v in fast[::997]), "its values")
s0 = f["streams"][0]
check(abs(muf.physical(s0, fast[1000][1])["PRESS"] - (101325 * 64 - 1000) * 15625e-6) < 1e-6, "physical")
slow = [n for n, _ in f["steps"][0x11]]
check(slow[:3] == [0, 1, 2] and 200 not in slow and 300 in slow, "the slow stream's pause is a jump")
ev = f["steps"][0x13]
check([(v[0], v[1]) for _, v in ev] == [(3000, 2), (7500, 3), (37930, 4)], "the events and their T_STEP")
info = f["steps"][0x05]
check(len(info) == 60 and info[5][1][3] == 1763500005 and info[5][1][4] == 5000, "the time's ties")
const = f["steps"][0x06]
check(len(const) == 1 and const[0][1][0] == -3000, "the constants")
check(f["table"].startswith("kind;name;motor\n") and '"Test; one"' in f["table"], "the set's table")
check(f["end"]["why"] == 16 and f["end"]["fast_steps"] == 60000, "the end")
check(all(k == 0x11 for k, _ in f.get("pauses", [])), "the index foretells every first step, but where the slow stream paused")

# --- the same file written by the Python twin: byte for byte the C's ---
def sample_py(path, room=0):
    img, pos = bytearray(512 * room), [0]
    if room:                                            # the room in memory: the head mirrored at its end
        def write(b):
            img[512 * pos[0]:512 * pos[0] + 512] = b
            pos[0] += 1
        place = lambda i, b: img.__setitem__(slice(512 * i, 512 * i + 512), b)
    else:
        out = open(path, "wb")
        write, place = out.write, None
    head = dict(letter="X", session=7, unit=3, chip=0x1122334455667788, fw=(0, 6, 0), time_source=1,
                start_us=1763500000000000, tz_q=4, name="sample")
    streams = [(0x10, 1000000, 17, 28, 0), (0x11, 10000, 80, 6, 0), (0x13, 0, 96, 5, 1),
               (0x05, 1000, 28, 17, 0), (0x06, 0, 1, 48, 0)]
    G = lambda st, fi, n, t, e, m, name, unit="", ref=0, rng=0: dict(stream=st, first=fi, count=n, type=t,
        exp10=e, mantissa=m, name=name, unit=unit, ref=ref, range=rng)
    groups = [G(0x10, 0, 3, "h", -6, 488, "ACC", "g", 0, 16), G(0x10, 3, 3, "h", -6, 10417, "ACC_HG", "g", 1, 320),
              G(0x10, 6, 3, "h", -3, 70, "GYR", "dps", 0, 4000), G(0x10, 9, 3, "h", -9, 1, "MAG", "T", 0, 800),
              G(0x10, 12, 1, "i", -6, 15625, "PRESS", "Pa"), G(0x11, 0, 1, "H", -2, 1, "HUM", "%RH", 0, 100),
              G(0x11, 1, 1, "h", -2, 1, "TEMP", "degC", 0, 125), G(0x11, 2, 1, "h", -2, 1, "PTEMP", "degC", 0, 85),
              G(0x13, 0, 1, "I", 0, 1, "T_STEP"), G(0x13, 1, 1, "B", 0, 1, "EVENT"),
              G(0x05, 0, 1, "h", -2, 1, "IMU_T", "degC"), G(0x05, 1, 1, "H", -3, 1, "CELL", "V"),
              G(0x05, 2, 1, "b", 0, 1, "RSSI", "dBm"), G(0x05, 3, 1, "I", 0, 1, "TIME_UTC", "s"),
              G(0x05, 4, 1, "I", 0, 1, "TIME_STP"), G(0x05, 5, 1, "i", -9, 1, "TIME_NS", "s"),
              G(0x06, 0, 3, "i", -9, 1, "MAG_OFF", "T"), G(0x06, 3, 9, "i", -6, 1, "MAG_MAT")]
    const = struct.pack("<12I", *[(k * 1000 - 3000) & 0xFFFFFFFF for k in range(12)])
    w = mufwrite.Writer(write, head, streams, groups, 'kind;name;motor\nrocket;"Test; one";C6-3\n', const,
                        place, room)
    for n in range(60000):
        w.step(0, struct.pack("<12hI", *[((n + k) % 2000) - 1000 for k in range(12)], 101325 * 64 - n))
        if n % 100 == 0 and not (20000 <= n < 30000):
            w.step(1, struct.pack("<HhH", 5000 + n // 100, 2000 - n // 100, 2500))
        if n == 30000:
            w.skip(1, 0)
        if n == 20000:
            w.skip(1, 100)
        if n % 1000 == 0:
            w.step(3, struct.pack("<HHbIIi", 2400, 3300, -80, 1763500000 + n // 1000, n, 0))
        if n in (3000, 7500, 37930):
            w.step(2, struct.pack("<IB", n, {3000: 2, 7500: 3, 37930: 4}[n]))
    w.end(16)
    if room:
        open(path, "wb").write(img)
    else:
        out.close()

py_path = os.path.join(tmp, "sample_py.muf")
sample_py(py_path)
check(filecmp.cmp(path, py_path, shallow=False), "the Python writer's file is the C writer's, byte for byte")

# --- the C reader against the Python reader: the same CSV ---
m2c = os.path.join(tmp, "muf2csv")
subprocess.check_call(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-O2", "-o", m2c,
                       os.path.join(here, "..", "tools", "muf2csv.c"), os.path.join(here, "..", "muf", "muf_read.c"), "-lm"])
dc, dp = os.path.join(tmp, "c"), os.path.join(tmp, "py")
os.makedirs(dc)
subprocess.check_call([m2c, path, dc])
subprocess.check_call([sys.executable, os.path.join(here, "..", "tools", "muf.py"), path, "--csv", dp])
names = sorted(os.listdir(dp))
check(names == sorted(os.listdir(dc)) and len(names) == 5, "the same five CSV files from both readers")
check(all(filecmp.cmp(os.path.join(dc, n), os.path.join(dp, n), shallow=False) for n in names),
      "the C reader's CSV is the Python reader's, byte for byte")
# --- the head's mirror: the C and the Python write it alike; read with the head destroyed ---
ROOM = 4000
mc, mp = os.path.join(tmp, "m_c.muf"), os.path.join(tmp, "m_py.muf")
subprocess.check_call([exe, mc, str(ROOM)])
sample_py(mp, ROOM)
check(filecmp.cmp(mc, mp, shallow=False), "the mirrored file the same from both writers")
img = open(mc, "rb").read()
check(len(img) == 512 * ROOM and img[-1024:] == img[:1024], "the head's two blocks again in the room's last two")
bad = os.path.join(tmp, "m_bad.muf")
open(bad, "wb").write(b"\xff" * 512 + img[512:])           # the head destroyed
fb = muf.read(bad)
check(fb["steps"] == f["steps"] and fb["table"] == f["table"] and fb["end"] == f["end"],
      "the Python reader reads every step through the mirror")
db = os.path.join(tmp, "cb")
os.makedirs(db)
subprocess.check_call([m2c, bad, db])
check(all(filecmp.cmp(os.path.join(dc, n), os.path.join(db, n), shallow=False) for n in names),
      "the C reader reads the same CSV through the mirror")
blocks = os.path.getsize(path) // 512
check(f["indexes"] == (blocks - 3 + 200) // 201, "an index after every 200 numbered blocks")
print("%d blocks, %d index blocks, %s" % (blocks, f["indexes"], "all good" if not fails else "%d failed" % fails))
sys.exit(1 if fails else 0)
