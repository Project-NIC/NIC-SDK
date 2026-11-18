"""NIC-SDK — MUF's writer in Python, the twin of muf/muf.c, written to the same bytes: tried on a
computer with nothing compiled, and the C held to it (tests/test_muf.py)."""
import struct, zlib

BLOCK, STEPS_FROM, INDEX_EVERY, INDEX_SLOTS = 512, 28, 200, 17


def _text(s, n):
    b = s.encode()[:n]
    return b + b"\0" * (n - len(b))


class Writer:
    def __init__(self, write, head, streams, groups, table_csv="", constants=None):
        """head: dict letter, session, unit, chip, fw (3), time_source, start_us, tz_q, name;
        streams: (kind, rate_mhz, steps_per_block, step_bytes, flags); groups: dicts as MUF.md."""
        self.write, self.h, self.streams = write, head, streams
        self.number, self.dropped = 0, 0
        self.buf = [bytearray(BLOCK) for _ in streams]
        self.filled = [0] * len(streams)
        self.next = [0] * len(streams)
        more = (len(groups) - 8 + 12) // 13 if len(groups) > 8 else 0
        b = bytearray(BLOCK)
        b[0:4] = b"MUF" + head["letter"].encode()
        b[4], b[5] = 1, 0x01
        struct.pack_into("<HBQ", b, 6, head["session"], head["unit"], head["chip"])
        b[17:20] = bytes(head["fw"])
        b[20], b[21], b[22], b[23] = len(streams), len(groups), 1 + more, head["time_source"]
        struct.pack_into("<Qb", b, 24, head["start_us"], head["tz_q"])
        b[33:49] = _text(head["name"], 16)
        for s, st in enumerate(streams):
            struct.pack_into("<BIBBB", b, 64 + 8 * s, *st)
        for g, gr in enumerate(groups[:8]):
            self._group(b, 208 + 36 * g, gr)
        self._emit(b)
        for c in range(more):
            b = bytearray(BLOCK)
            b[0:4] = b"MUF" + head["letter"].encode()
            b[4], b[5] = 1, 0x02
            struct.pack_into("<H", b, 6, head["session"])
            for k, gr in enumerate(groups[8 + 13 * c:8 + 13 * c + 13]):
                self._group(b, 12 + 36 * k, gr)
            self._emit(b)
        self._range_start()
        t = table_csv.encode()
        for off in range(0, len(t), 494):
            b = bytearray(BLOCK)
            b[0] = 0x04
            struct.pack_into("<H", b, 2, head["session"])
            struct.pack_into("<H", b, 12, len(t))
            part = t[off:off + 494]
            b[14:14 + len(part)] = part
            self._numbered(b)
        for st in streams:
            if st[0] == 0x06 and constants is not None:
                b = bytearray(BLOCK)
                b[0], b[1] = 0x06, 1
                struct.pack_into("<H", b, 2, head["session"])
                b[STEPS_FROM:STEPS_FROM + st[3]] = constants
                self._numbered(b)

    @staticmethod
    def _group(b, off, g):
        struct.pack_into("<BBBcBbhi", b, off, g["stream"], g["first"], g["count"], g["type"].encode(),
                         g.get("flags", 0), g["exp10"], g["mantissa"], g.get("add", 0))
        b[off + 12:off + 20] = _text(g["name"], 8)
        b[off + 20:off + 28] = _text(g.get("unit", ""), 8)
        b[off + 28] = g.get("ref", 0)
        struct.pack_into("<H", b, off + 29, g.get("range", 0))

    def _emit(self, b):
        struct.pack_into("<I", b, 508, zlib.crc32(bytes(b[:508])))
        self.write(bytes(b))

    def _range_start(self):
        self.range_first, self.range = self.number, []
        nexts = [self.next[s] - self.filled[s] for s, st in enumerate(self.streams) if st[0] != 0x06]
        self.range_next = (nexts + [0] * INDEX_SLOTS)[:INDEX_SLOTS]

    def _index(self):
        b = bytearray(BLOCK)
        b[0] = 0x07
        struct.pack_into("<HII", b, 2, self.h["session"], self.number, 0)
        struct.pack_into("<I", b, 12, self.range_first)
        struct.pack_into("<17I", b, 28, *self.range_next)
        for k, (kind, steps) in enumerate(self.range):
            b[96 + 2 * k], b[97 + 2 * k] = kind, steps
        self.number += 1
        self._emit(b)
        self._range_start()

    def _numbered(self, b):
        struct.pack_into("<I", b, 4, self.number)
        self.range.append((b[0], b[1]))
        self.number += 1
        self._emit(b)
        if len(self.range) == INDEX_EVERY:
            self._index()

    def flush(self, s):
        if not self.filled[s]:
            return
        b = self.buf[s]
        b[0], b[1] = self.streams[s][0], self.filled[s]
        struct.pack_into("<H", b, 2, self.h["session"])
        struct.pack_into("<I", b, 8, self.next[s] - self.filled[s])
        self.filled[s] = 0
        self._numbered(b)
        self.buf[s] = bytearray(BLOCK)

    def step(self, s, data):
        sb, spb = self.streams[s][3], self.streams[s][2]
        off = STEPS_FROM + self.filled[s] * sb
        self.buf[s][off:off + sb] = data
        self.filled[s] += 1
        self.next[s] += 1
        if self.filled[s] == spb:
            self.flush(s)

    def skip(self, s, steps):
        self.flush(s)
        self.next[s] += steps

    def end(self, why):
        fast = 0
        for s, st in enumerate(self.streams):
            self.flush(s)
            if st[0] == 0x10:
                fast = self.next[s]
        if self.range:
            self._index()
        b = bytearray(BLOCK)
        b[0] = 0x03
        struct.pack_into("<HI", b, 2, self.h["session"], self.number)
        b[12] = why
        struct.pack_into("<III", b, 16, fast, self.number + 1, self.dropped)
        self.number += 1
        self._emit(b)
