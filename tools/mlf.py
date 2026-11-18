"""MLF — the Matroshka Log File, the Python reference of its core.

A file as `MLF.md` fixes it: the header and its sections, the data — segments and configs, each
COBS-framed and followed by a zero — growing up from the header, and the description's 16 B
entries growing down from the end. Writes and reads a whole file in memory; what a segment's body
holds is the profile's. The C (`mlf/mlf.c`) writes the same bytes.
"""

from __future__ import annotations

import struct
import zlib
from dataclasses import dataclass, field
from typing import Iterator, List, Optional, Sequence, Tuple

MAGIC = b"MLF\x00"
FORMAT = 1
MB = 1 << 20
SEG, CFG = 1, 2                      # an entry's type
SYNC = {SEG: b"MSEG", CFG: b"MCFG"}
CODEC = 128                          # the core's section: a codec's table
RAW, NRC = 0, 1                      # a segment's codec


# ---------------------------------------------------------------- checks and framing

def crc32(b: bytes) -> int:
    """IEEE 802.3, the one zlib computes."""
    return zlib.crc32(b) & 0xFFFFFFFF


def crc16(b: bytes) -> int:
    """CRC-16/CCITT-FALSE: polynomial 0x1021, initial 0xFFFF, no reflection, no final XOR."""
    c = 0xFFFF
    for x in b:
        c ^= x << 8
        for _ in range(8):
            c = ((c << 1) ^ 0x1021) & 0xFFFF if c & 0x8000 else (c << 1) & 0xFFFF
    return c


def cobs_encode(data: bytes) -> bytes:
    """COBS as Cheshire and Baker's encoder writes it: a block of up to 254 bytes behind its code,
    and a data that ends on a full block closed by an empty one."""
    out = bytearray(b"\x00")
    at, code = 0, 1
    for b in data:
        if b == 0:
            out[at] = code
            at, code = len(out), 1
            out.append(0)
        else:
            out.append(b)
            code += 1
            if code == 0xFF:
                out[at] = code
                at, code = len(out), 1
                out.append(0)
    out[at] = code
    return bytes(out)


def cobs_decode(enc: bytes) -> bytes:
    out = bytearray()
    i, n = 0, len(enc)
    while i < n:
        c = enc[i]
        if c == 0 or i + c > n:
            raise ValueError("MLF: COBS damaged")
        block = enc[i + 1:i + c]
        if 0 in block:
            raise ValueError("MLF: COBS damaged")
        out += block
        i += c
        if c < 0xFF and i < n:
            out.append(0)
    return bytes(out)


def framed(obj: bytes) -> bytes:
    return cobs_encode(obj) + b"\x00"


# ---------------------------------------------------------------- sections

@dataclass(frozen=True)
class Section:
    id: int
    body: bytes


def sections_bytes(sections: Sequence[Section]) -> bytes:
    return b"".join(struct.pack("<B3xI", s.id, len(s.body)) + s.body for s in sections)


def parse_sections(b: bytes) -> List[Section]:
    out, i = [], 0
    while i < len(b):
        if i + 8 > len(b):
            raise ValueError("MLF: a section cut short")
        sid, ln = struct.unpack_from("<B3xI", b, i)
        if i + 8 + ln > len(b):
            raise ValueError("MLF: a section cut short")
        out.append(Section(sid, bytes(b[i + 8:i + 8 + ln])))
        i += 8 + ln
    return out


def in_force(old: Sequence[Section], new: Sequence[Section]) -> List[Section]:
    """A config's sections over those in force: every id it carries replaces all of that id."""
    ids = {s.id for s in new}
    return [s for s in old if s.id not in ids] + list(new)


def shortest(bodies: Sequence[Tuple[int, bytes]]) -> Tuple[int, bytes]:
    """The codec set (`MLF.md`, §4): of (codec, body) the shortest body, the lower codec on a tie."""
    return min(bodies, key=lambda cb: (len(cb[1]), cb[0]))


# ---------------------------------------------------------------- the header

@dataclass
class Header:
    sequence: int
    first_second: int
    source: int
    size: int
    sections: List[Section] = field(default_factory=list)
    length: int = 0


def header_bytes(h: Header) -> bytes:
    body = sections_bytes(h.sections)
    length = 20 + len(body) + 4
    b = MAGIC + struct.pack("<BBHIII", FORMAT, h.size, h.sequence, length, h.first_second,
                            h.source) + body
    return b + struct.pack("<I", crc32(b))


def parse_header(b: bytes) -> Header:
    if len(b) < 24 or b[:4] != MAGIC or b[4] != FORMAT:
        raise ValueError("MLF: not an MLF file")
    size, sequence, length, first, source = struct.unpack_from("<BHIII", b, 5)
    if length < 24 or length > len(b) or struct.unpack_from("<I", b, length - 4)[0] != crc32(b[:length - 4]):
        raise ValueError("MLF: the header is damaged")
    if size > 7:
        raise ValueError("MLF: a size past 128 MB")
    return Header(sequence, first, source, size, parse_sections(b[20:length - 4]), length)


# ---------------------------------------------------------------- segments and configs

def segment_bytes(first: int, last: int, codec: int, body: bytes) -> bytes:
    b = b"MSEG" + struct.pack("<IIIB3x", 20 + len(body) + 4, first, last, codec) + body
    return b + struct.pack("<I", crc32(b))


def config_bytes(from_second: int, sections: Sequence[Section]) -> bytes:
    body = sections_bytes(sections)
    b = b"MCFG" + struct.pack("<II4x", 16 + len(body) + 4, from_second) + body
    return b + struct.pack("<I", crc32(b))


def parse_object(b: bytes) -> Tuple[int, dict]:
    """A segment or a config, unframed: (SEG, {first, last, codec, body}) or (CFG, {from,
    sections}); ValueError when it is not whole."""
    if len(b) < 20 or b[:4] not in (b"MSEG", b"MCFG"):
        raise ValueError("MLF: no sync")
    length = struct.unpack_from("<I", b, 4)[0]
    if length != len(b) or struct.unpack_from("<I", b, length - 4)[0] != crc32(b[:length - 4]):
        raise ValueError("MLF: an object is damaged")
    if b[:4] == b"MSEG":
        if length < 24:
            raise ValueError("MLF: a segment cut short")
        first, last, codec = struct.unpack_from("<IIB", b, 8)
        return SEG, {"first": first, "last": last, "codec": codec, "body": bytes(b[20:length - 4])}
    return CFG, {"from": struct.unpack_from("<I", b, 8)[0], "sections": parse_sections(b[16:length - 4])}


def entry_bytes(second: int, offset: int, length: int, type_: int) -> bytes:
    b = struct.pack("<IIIBx", second, offset, length, type_)
    return b + struct.pack("<H", crc16(b))


def parse_entry(b: bytes) -> Optional[Tuple[int, int, int, int]]:
    """(second, offset, length, type), or None for a free or damaged entry."""
    if struct.unpack_from("<H", b, 14)[0] != crc16(b[:14]) or b[12] not in (SEG, CFG):
        return None
    second, offset, length, type_ = struct.unpack_from("<IIIB", b)
    return second, offset, length, type_


# ---------------------------------------------------------------- a file

class Writer:
    """A file in memory: the header, then each object appended as the writer writes it — its framed
    bytes first, then its entry. `append` returns False when the file is full."""

    def __init__(self, header: Header):
        self.size = MB << header.size
        self.image = bytearray(self.size)
        hb = header_bytes(header)
        self.image[:len(hb)] = hb
        self.end = len(hb)
        self.entries = 0
        self.last = 0
        self.sections = list(header.sections)

    def append(self, obj: bytes, type_: int, second: int) -> bool:
        if second < self.last:
            raise ValueError("MLF: an entry's second goes back")
        fb = framed(obj)
        desc = self.size - 16 * (self.entries + 1)
        if self.end + len(fb) > desc:
            return False
        self.image[self.end:self.end + len(fb)] = fb
        self.image[desc:desc + 16] = entry_bytes(second, self.end, len(fb) - 1, type_)
        self.end += len(fb)
        self.entries += 1
        self.last = second
        return True

    def segment(self, first: int, last: int, codec: int, body: bytes) -> bool:
        return self.append(segment_bytes(first, last, codec, body), SEG, last)

    def config(self, from_second: int, sections: Sequence[Section]) -> bool:
        if not self.append(config_bytes(from_second, sections), CFG, from_second):
            return False
        self.sections = in_force(self.sections, sections)
        return True


class Reader:
    """A whole file: the header, the description's good entries, and the objects they point at,
    in the order written, each segment with the sections in force where it lies."""

    def __init__(self, image: bytes):
        self.image = bytes(image)
        self.header = parse_header(self.image)
        if len(self.image) != MB << self.header.size:
            raise ValueError("MLF: the file is not its size")
        self.entries = self._entries()

    def _entry(self, i: int):
        at = len(self.image) - 16 * (i + 1)
        return parse_entry(self.image[at:at + 16])

    def _entries(self) -> List[Optional[Tuple[int, int, int, int]]]:
        """The written entries are good ones, a free one all zeros and failing its CRC, so a binary
        search finds the last good entry; a damaged one the search stepped over is None."""
        lo, hi = 0, (len(self.image) - self.header.length) // 16
        while lo < hi:
            mid = (lo + hi) // 2
            if self._entry(mid) is None:
                hi = mid
            else:
                lo = mid + 1
        return [self._entry(i) for i in range(lo)]

    def _second(self, i: int) -> int:
        """An entry's second; a damaged one takes the last good one's before it, 0 with none."""
        while i >= 0 and self.entries[i] is None:
            i -= 1
        return self.entries[i][0] if i >= 0 else 0

    def find(self, second: int) -> int:
        """The index of the first entry whose second is not before `second`."""
        lo, hi = 0, len(self.entries)
        while lo < hi:
            mid = (lo + hi) // 2
            if self._second(mid) < second:
                lo = mid + 1
            else:
                hi = mid
        return lo

    def object(self, i: int) -> Tuple[int, dict]:
        if self.entries[i] is None:
            raise ValueError("MLF: a damaged entry")
        second, offset, length, type_ = self.entries[i]
        if offset + length >= len(self.image) or self.image[offset + length] != 0:
            raise ValueError("MLF: an entry points past its object")
        t, obj = parse_object(cobs_decode(self.image[offset:offset + length]))
        if t != type_:
            raise ValueError("MLF: an entry of the wrong type")
        return t, obj

    def objects(self) -> Iterator[Tuple[int, dict, Optional[List[Section]]]]:
        """(type, object, sections): a segment with the sections in force where it lies, for its
        profile to decode; a config with the sections it brought in force; a damaged object as
        (0, {"entry": …}, None)."""
        sections = list(self.header.sections)
        for i in range(len(self.entries)):
            try:
                t, obj = self.object(i)
            except ValueError:
                yield 0, {"entry": self.entries[i]}, None
                continue
            if t == CFG:
                sections = in_force(sections, obj["sections"])
            yield t, obj, sections


def scan(image: bytes, start: int) -> Iterator[Tuple[int, dict]]:
    """A file whose description is lost, or a packet cut short: every whole object between the
    zeros from `start` on."""
    i, n = start, len(image)
    while i < n:
        j = image.find(b"\x00", i)
        if j < 0:
            j = n
        if j > i:
            try:
                yield parse_object(cobs_decode(image[i:j]))
            except ValueError:
                pass
        i = j + 1
