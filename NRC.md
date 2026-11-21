# NRC — the NIC Rice Codec, its core

**A lossless coder of integer series into one bit stream, and back.** Not a format: it has no file
of its own. An MLF segment carries its stream as codec 1 ([`MLF.md`](MLF.md)), or a container's
data is packed by it for carrying or keeping — the container around it giving the length and the
CRC. **A stream depends on nothing outside itself and the profile's reading of the records**: no
state carries from one stream to the next, no table is trained, no model adapts. **NRC has no
versions** (§9).

## 1 In plain words — no table, a rule

**NRC stores no table and builds none.** A Huffman or an ANS code needs a table of how often each
symbol comes, and the reader needs that table to decode. NRC does without one: **a Rice code is a
rule, not a table** — one small number, `k`, says how to write every value of a run of 32, and that
number travels in the stream in front of the 32. The predictor's coefficients travel the same way,
in the series they serve.

**Four steps, one series at a time:**

1. **Predict.** Guess each value from the ones before it — the same as the last (ORDER 1), on the
   line through the last two (ORDER 2), or from the last eight (LPC); or say every value is equal
   (CONST), or give up and write them as they are (RAW).
2. **Keep the miss.** Write only the difference between the value and the guess, the residual.
3. **Fold the sign.** 0, −1, +1, −2, +2 … become 0, 1, 2, 3, 4 …
4. **Rice-code it.** Pick `k` for the next 32 residuals; each is written as `u >> k` ones and a zero,
   then its `k` low bits.

**An example** — a temperature series in hundredths of a degree, 16-bit: `2150 2151 2151 2153
2152`. ORDER 1 keeps the misses `+1 0 +2 −1`, folded `2 0 4 1`. With `k` 1 they are written
`10·0  0·0  110·0  0·1` — **11 bits for four values that raw take 64**. With the bookkeeping — the
mode, the first value, the `k` — the series is 35 bits against 83 for RAW; over thousands of values
the bookkeeping vanishes and what is left is the sensor's own noise.

**The coder tries every mode and keeps the shortest**, per series. The family is FLAC's and
Shorten's — prediction and Rice codes — fitted to sensors' series rather than sound.

## 2 The stream

**Bits are written most significant first; the stream ends padded with zero bits to a whole byte.**
Its parts and their order are the profile's (§7); NRC gives them two things to write with:

- **γ — the Elias gamma code** of an integer `m ≥ 1`: `L = ⌊log₂ m⌋` zero bits, then `m` in `L + 1`
  bits — for a count, written as γ(count + 1) where it may be 0.
- **A series** (§3).

## 3 A series

`n` values of `w` bits, `n` and `w` known to both sides from the profile. Nothing is written when `n`
is 0. **Where the profile names a reference series**, one bit first: 1 — the series is coded as
`xᵢ − yᵢ` modulo 2^w, `y` the reference series, of the same `n` and `w`, already decoded; 0 — as
itself. Then a 3-bit mode:

| mode | | written |
|---|---|---|
| 0 | **CONST** | one value, `w` bits — every value is equal |
| 1 | **ORDER 1** | `x₀` in `w` bits, then the residuals `rᵢ = xᵢ − xᵢ₋₁` for `i` = 1 … `n − 1` |
| 2 | **ORDER 2** | `x₀`, `x₁` in `w` bits, then `rᵢ = xᵢ − 2xᵢ₋₁ + xᵢ₋₂` for `i` = 2 … `n − 1` |
| 3 | **LPC** | `p − 1` in 3 bits, `s` in 4 bits, `p` coefficients `a₁ … aₚ` in 15 bits signed, `x₀ … xₚ₋₁` in `w` bits, then `rᵢ = xᵢ − ((a₁xᵢ₋₁ + … + aₚxᵢ₋ₚ) >> s)` for `i` = `p` … `n − 1` |
| 4 | **RAW** | `n` values, `w` bits each |
| 5–7 | — | free; a stream carrying one is damaged to a reader that does not know it |

**The values are unsigned integers of `w` bits** to every mode but LPC: the arithmetic is modulo
2^w, and the same bits come back whatever they mean. **LPC reads them with their sign** — a series
the profile marks signed as a signed `w`-bit number, any other as unsigned — and forms the sum in
64-bit signed integers, the shift `s` arithmetic.

**A residual is taken modulo 2^w and read as a signed `w`-bit number**, so it always fits in `w` bits,
and the decoder adds it back modulo 2^w. It is then folded to an unsigned number,
`u = (r << 1) ⊕ (r >> (w − 1))` with an arithmetic shift.

**The residuals go in partitions of 32**, in order, the last one shorter. Each partition opens with
a **5-bit parameter `k`**:

- **`k` 0 … 30 — a Rice code**: each `u` as `q = u >> k` one-bits and a zero, then the `k` low bits
  of `u`.
- **`k` 31 — the escape**: each `u` in `w` bits.

## 4 The coder's choices

**The stream is a function of its input.** The decoder accepts any valid choice, but the coder
makes exactly these, so every implementation writes the same bytes and a test compares them:

- **Per partition**: the `k` from 0 to min(30, `w`) that writes the fewest bits, the smaller `k` on a
  tie; the escape where it writes no more than that.
- **The predictor**: the autocorrelation of the series for lags 0 … 8 summed exactly — a 32-bit
  value's products reach 2^64 and a long series' sums 2^80, so 64 bits overflow and 128 do not —
  and rounded to the nearest double, ties to even; the Levinson–Durbin recursion on it in IEEE-754
  double, in the order the recursion is written, without fused multiply-add, giving the
  coefficients for every order 1 … 8 it reaches; each set quantised with the largest `s` ≤ 15 that
  keeps every |`aⱼ`| · 2^s below 2^14, rounded half away from zero. Every order is tried.
- **The reference**: tried where the profile names one, the series coded both ways.
- **Per series**: the reference choice and the mode — and under LPC the order — that write the
  fewest bits; on a tie no reference, the lower mode, the lower order. CONST only when every value
  is equal.

**The worst case is bounded**: a stream is never longer than its values written raw by more than
its own bookkeeping — four bits a series and five a partition — so a container that keeps the
shorter of NRC and raw never pays more than raw.

## 5 Decoding

The decoder holds the profile's reading of the records and reads the parts in the order the
profile gives. **A stream that does not end within its last byte, or ends with bits left over in
more than that byte, is damaged** — with a container's CRC already whole, a coder fault, never a
medium fault. **So is a stream that says what no coder writes**, checked before it is trusted: a
mode 5–7; mode 2 on fewer than two values; LPC of an order not below `n`; a count above the
profile's ceiling (§7) — the one number a few damaged bits can turn into gigabytes. A coder holds
the same ceiling and never writes past it.

## 6 Why it has this shape

- **Columns, not rows.** A record is a row of fields; what is alike is one field down the records
  — an axis, a field component, a counter — so the profile turns the rows into series, one per
  field, and NRC codes each on its own. That is the shape Steim and FLAC code.
- **Prediction, Rice codes, partitions.** A sampled quantity changes little from one record to the
  next, and a Rice code writes a small number in a few bits. The parameter is chosen per 32
  values, so an event costs bits where it happens and the quiet rest stays cheap.
- **Better than Steim-2, and where from.** Steim-2 packs first differences into 32-bit words whose
  fields share one width; NRC writes bit by bit, predicts up to order 8 where the signal is
  coloured, and **codes one series against another** where two sensors see the same quantity.
- **Widths from the profile.** A difference is taken on the value as it was written — 2 B, 3 B, 4 B
  — never byte by byte across a multi-byte value. **A wrong width costs compression, never data.**
- **Every stream is a keyframe**: read alone, and a damaged one costs itself only.
- **Integers only in the decoder, and nothing to store.** Shifts, adds, multiplies and compares; no
  table in ROM. The coder alone computes in floating point, to find the predictor, in a fixed
  order so its output is fixed too.

## 7 What the profile says

- **The records' series**: how the records are cut into series, each with its `n`, its `w`, its
  sign and its reference — always a series already written.
- **The parts of the stream and their order**: the counts written as γ, the series, and anything
  that tells the decoder which records fall where.
- **The ceiling of a count**, from the largest stream its container holds.
- **How the records are rebuilt** from the decoded series, in their order.

## 8 What is assumed, and measured

**NRC is assumed to code a series in no more than Steim-2 does, and a coloured or paired series in
less.** On recorded open series, in bits a sample, the series alone, a stream every 8 s and longer
(`tools/nrcmeasure.py`; the README says how the series are fetched):

| series | stream | Steim-2 | NRC |
|---|---|---|---|
| GE.MORC HH?, 100 Hz, 20 min quiet (2024-01-10 00:00) | 8 s · 32 s | 5,97–6,04 · 5,72–5,76 | 4,98–5,03 · 4,83–4,91 |
| GE.MORC HH?, 100 Hz, an earthquake's surface waves (2024-01-01 07:20, 20 min) | 8 s · 32 s | 8,39–8,62 · 8,06–8,30 | 5,79–6,36 · 5,68–6,09 |
| BDV, Budkov, 1 Hz, X Y Z F, a day (2024-01-10) | 8 s · 64 s | 64 · 8,00–9,65 | 6,41–8,47 · 3,36–5,72 |
| BDV, 1 min, a day | 1 h · a day | 8,53–16,71 · 6,04–8,18 | 5,46–7,49 · 5,12–7,07 |

**LPC carries a series near zero, not one far from it**: on the quiet seismometer's counts it takes
0,15–0,3 bit a sample off ORDER 2 at 32 s; on the magnetometer's tens of thousands of nT it loses to
ORDER 1 every time — it has no mean term, and a coefficient quantised to 2⁻¹⁴ against a value of
2 · 10⁶ leaves an error of ~100. ORDER 1 and 2 carry such a series, and the coder takes the cheaper.
The ratios are settled for good only against millions of recorded streams.

## 9 What never changes

1. **Nothing is ever moved or given a new meaning**: the modes 0–4, the partition of 32, the 5-bit
   `k` and its escape, γ, the reference bit.
2. **It grows only where it left room**: the modes 5–7.
3. **No versions.** A change that would break rule 1 is another codec, under another number in its
   container's set.
4. **A profile changes only itself** — its series, its parts, its ceiling.
