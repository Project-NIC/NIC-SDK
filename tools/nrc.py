"""NRC — the NIC Rice Codec, the Python reference of its core.

One series of n values of w bits to a bit stream and back, losslessly, exactly as `NRC.md` fixes
it: prediction (CONST, ORDER 1 and 2, LPC to order 8, RAW), the residual folded, Rice codes in
partitions of 32 with an escape, an optional reference series, Elias gamma for counts. The coder's
choices are the document's, so the C (`nrc/nrc.c`) and this write the same bytes. A profile cuts
its records into series and writes them in its own order (`NRC.md`, §7).
"""

from __future__ import annotations

from typing import List, Optional, Sequence, Tuple

# ---------------------------------------------------------------- bit I/O

class BitWriter:
    __slots__ = ("buf", "acc", "nbits")

    def __init__(self) -> None:
        self.buf = bytearray()
        self.acc = 0
        self.nbits = 0

    def write(self, value: int, width: int) -> None:
        if width == 0:
            return
        self.acc = (self.acc << width) | (value & ((1 << width) - 1))
        self.nbits += width
        while self.nbits >= 8:
            self.nbits -= 8
            self.buf.append((self.acc >> self.nbits) & 0xFF)
        self.acc &= (1 << self.nbits) - 1

    def unary(self, q: int) -> None:
        while q > 0:
            n = min(q, 32)
            self.write((1 << n) - 1, n)
            q -= n
        self.write(0, 1)

    def gamma(self, m: int) -> None:
        assert m >= 1
        length = m.bit_length() - 1
        self.write(0, length)
        self.write(m, length + 1)

    def bytes(self) -> bytes:
        if self.nbits:
            self.buf.append((self.acc << (8 - self.nbits)) & 0xFF)
            self.acc = 0
            self.nbits = 0
        return bytes(self.buf)

    def bit_length(self) -> int:
        return len(self.buf) * 8 + self.nbits


class BitReader:
    __slots__ = ("data", "pos", "end")

    def __init__(self, data: bytes) -> None:
        self.data = data
        self.pos = 0
        self.end = len(data) * 8

    def read(self, width: int) -> int:
        if width == 0:
            return 0
        if self.pos + width > self.end:
            raise ValueError("NRC stream damaged: read past its end")
        v = 0
        p = self.pos
        for _ in range(width):
            v = (v << 1) | ((self.data[p >> 3] >> (7 - (p & 7))) & 1)
            p += 1
        self.pos = p
        return v

    def unary(self) -> int:
        q = 0
        while self.read(1):
            q += 1
        return q

    def gamma(self) -> int:
        length = 0
        while self.read(1) == 0:
            length += 1
            if length > 32:
                raise ValueError("NRC stream damaged: gamma code")
        return (1 << length) | self.read(length)


# ---------------------------------------------------------------- a series

MAX_ORDER = 8


def _levinson(r: Sequence[float], max_order: int) -> List[List[float]]:
    """Levinson–Durbin, in the order written, no fused multiply-add: the coefficient sets for
    every order it reaches, a[p] = [a1 .. ap] with x̂ᵢ = Σ aⱼ xᵢ₋ⱼ."""
    sets: List[List[float]] = []
    if r[0] <= 0.0:
        return sets
    err = r[0]
    a: List[float] = []
    for p in range(1, max_order + 1):
        acc = r[p]
        for j in range(1, p):
            acc = acc - a[j - 1] * r[p - j]
        if err <= 0.0:
            break
        k = acc / err
        if not (-1.0 < k < 1.0):
            break
        new = [0.0] * p
        for j in range(1, p):
            new[j - 1] = a[j - 1] - k * a[p - j - 1]
        new[p - 1] = k
        a = new
        err = err * (1.0 - k * k)
        sets.append(list(a))
    return sets


def _quantise(a: Sequence[float]) -> Optional[Tuple[int, List[int]]]:
    """The largest s ≤ 15 that keeps every |aⱼ|·2^s below 2^14, rounded half away from zero."""
    amax = max(abs(x) for x in a)
    if amax == 0.0:
        return 15, [0] * len(a)
    s = 15
    while s >= 0 and amax * (1 << s) >= (1 << 14):
        s -= 1
    if s < 0:
        return None
    q = []
    for x in a:
        v = x * (1 << s)
        qi = int(abs(v) + 0.5)
        q.append(qi if v >= 0 else -qi)
    if any(abs(qi) >= (1 << 14) for qi in q):
        return None
    return s, q


def _to_signed(v: int, w: int) -> int:
    return v - (1 << w) if v & (1 << (w - 1)) else v


def _fold(r: int, w: int) -> int:
    r = _to_signed(r & ((1 << w) - 1), w)
    return (r << 1) ^ (r >> (w - 1)) if r < 0 else r << 1


def _unfold(u: int) -> int:
    return -(u >> 1) - 1 if u & 1 else u >> 1


def _partition_bits(us: Sequence[int], k: int, w: int) -> int:
    if k == 31:
        return 5 + w * len(us)
    return 5 + sum((u >> k) + 1 + k for u in us)


def _best_k(us: Sequence[int], w: int) -> int:
    best_k, best = 0, None
    for k in range(0, min(30, w) + 1):
        b = _partition_bits(us, k, w)
        if best is None or b < best:
            best, best_k = b, k
    if _partition_bits(us, 31, w) <= best:
        return 31
    return best_k


def _write_residuals(bw: BitWriter, rs: Sequence[int], w: int) -> None:
    us = [_fold(r, w) for r in rs]
    for i in range(0, len(us), 32):
        part = us[i:i + 32]
        k = _best_k(part, w)
        bw.write(k, 5)
        if k == 31:
            for u in part:
                bw.write(u, w)
        else:
            for u in part:
                bw.unary(u >> k)
                bw.write(u, k)


def _residual_bits(rs: Sequence[int], w: int) -> int:
    us = [_fold(r, w) for r in rs]
    total = 0
    for i in range(0, len(us), 32):
        part = us[i:i + 32]
        total += _partition_bits(part, _best_k(part, w), w)
    return total


def _read_residuals(br: BitReader, n: int, w: int) -> List[int]:
    out = []
    while len(out) < n:
        m = min(32, n - len(out))
        k = br.read(5)
        for _ in range(m):
            if k == 31:
                u = br.read(w)
            else:
                q = br.unary()
                u = (q << k) | br.read(k)
            out.append(_unfold(u))
    return out


def _lpc_candidates(xs: Sequence[int], w: int, signed: bool):
    n = len(xs)
    vals = [_to_signed(x, w) for x in xs] if signed else list(xs)
    r = []
    for lag in range(0, MAX_ORDER + 1):
        if lag >= n:
            break
        acc = 0
        for i in range(lag, n):
            acc += vals[i] * vals[i - lag]
        r.append(float(acc))
    if len(r) < 2:
        return []
    out = []
    for a in _levinson(r, min(MAX_ORDER, len(r) - 1)):
        q = _quantise(a)
        if q is None:
            continue
        s, coeffs = q
        p = len(coeffs)
        if p >= n:
            continue
        res = []
        for i in range(p, n):
            acc = 0
            for j in range(1, p + 1):
                acc += coeffs[j - 1] * vals[i - j]
            res.append((vals[i] - (acc >> s)) & ((1 << w) - 1))
        out.append((p, s, coeffs, res))
    return out


def _mode_bits(xs: Sequence[int], w: int, signed: bool):
    """Every mode's cost; returns a list of (bits, mode, payload) with the coder's tie rule
    (the lower mode, the lower order) applied by stable ordering."""
    n = len(xs)
    mask = (1 << w) - 1
    out = []
    if all(x == xs[0] for x in xs):
        out.append((3 + w, 0, None))
    if n >= 1:
        rs = [(xs[i] - xs[i - 1]) & mask for i in range(1, n)]
        out.append((3 + w + _residual_bits(rs, w), 1, rs))
    if n >= 2:
        rs = [(xs[i] - 2 * xs[i - 1] + xs[i - 2]) & mask for i in range(2, n)]
        out.append((3 + 2 * w + _residual_bits(rs, w), 2, rs))
    for p, s, coeffs, res in _lpc_candidates(xs, w, signed):
        out.append((3 + 3 + 4 + 15 * p + p * w + _residual_bits(res, w), 3, (p, s, coeffs, res)))
    out.append((3 + n * w, 4, None))
    return out


def _write_mode(bw: BitWriter, xs: Sequence[int], w: int, mode: int, payload) -> None:
    bw.write(mode, 3)
    if mode == 0:
        bw.write(xs[0], w)
    elif mode == 1:
        bw.write(xs[0], w)
        _write_residuals(bw, payload, w)
    elif mode == 2:
        bw.write(xs[0], w)
        bw.write(xs[1], w)
        _write_residuals(bw, payload, w)
    elif mode == 3:
        p, s, coeffs, res = payload
        bw.write(p - 1, 3)
        bw.write(s, 4)
        for c in coeffs:
            bw.write(c & 0x7FFF, 15)
        for i in range(p):
            bw.write(xs[i], w)
        _write_residuals(bw, res, w)
    else:
        for x in xs:
            bw.write(x, w)


def write_series(bw: BitWriter, xs: Sequence[int], w: int, signed: bool = False,
                 ref: Optional[Sequence[int]] = None) -> None:
    """One series of n values of w bits; `ref` the reference channel's values where the profile
    names one (the one leading bit is written then)."""
    n = len(xs)
    if n == 0:
        return
    mask = (1 << w) - 1
    best = None
    if ref is not None:
        cands = [(0, xs)]
        cands.append((1, [(x - y) & mask for x, y in zip(xs, ref)]))
    else:
        cands = [(None, xs)]
    for use_ref, vals in cands:
        for bits, mode, payload in _mode_bits(vals, w, signed):
            key = (bits, use_ref or 0, mode, (payload[0] if mode == 3 else 0))
            if best is None or key < best[0]:
                best = (key, use_ref, vals, mode, payload)
    _, use_ref, vals, mode, payload = best
    if ref is not None:
        bw.write(use_ref, 1)
    _write_mode(bw, vals, w, mode, payload)


def read_series(br: BitReader, n: int, w: int, signed: bool = False,
                ref: Optional[Sequence[int]] = None) -> List[int]:
    if n == 0:
        return []
    mask = (1 << w) - 1
    use_ref = br.read(1) if ref is not None else 0
    mode = br.read(3)
    if mode == 0:
        xs = [br.read(w)] * n
    elif mode == 1:
        xs = [br.read(w)]
        for r in _read_residuals(br, n - 1, w):
            xs.append((xs[-1] + r) & mask)
    elif mode == 2:
        if n < 2:
            raise ValueError("NRC stream damaged: order 2 on %d value" % n)
        xs = [br.read(w), br.read(w)]
        for r in _read_residuals(br, n - 2, w):
            xs.append((r + 2 * xs[-1] - xs[-2]) & mask)
    elif mode == 3:
        p = br.read(3) + 1
        if p >= n:
            raise ValueError("NRC stream damaged: LPC order %d on %d values" % (p, n))
        s = br.read(4)
        coeffs = [_to_signed(br.read(15), 15) for _ in range(p)]
        xs = [br.read(w) for _ in range(p)]
        vals = [_to_signed(x, w) for x in xs] if signed else list(xs)
        for r in _read_residuals(br, n - p, w):
            acc = 0
            for j in range(1, p + 1):
                acc += coeffs[j - 1] * vals[-j]
            v = (r + (acc >> s)) & mask
            xs.append(v)
            vals.append(_to_signed(v, w) if signed else v)
    elif mode == 4:
        xs = [br.read(w) for _ in range(n)]
    else:
        raise ValueError("NRC stream damaged: mode %d" % mode)
    if use_ref:
        xs = [(x + y) & mask for x, y in zip(xs, ref)]
    return xs


def finish(br: BitReader) -> None:
    """A stream must end within its last byte (`NRC.md`, §5)."""
    if br.end - br.pos >= 8:
        raise ValueError("NRC stream damaged: bits left over")
