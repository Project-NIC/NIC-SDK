/* NRC — the NIC Rice Codec, the C reference of its core; see nrc.h. Built with -std=c99
 * -ffp-contract=off: the predictor's double arithmetic must round every operation as
 * ../tools/nrc.py does, with no fused multiply-add. */
#if defined(__clang__)
#pragma STDC FP_CONTRACT OFF
#endif

#include "nrc.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define MAX_ORDER 8

/* ------------------------------------------------------------------ bit I/O */

static void bw_put(nrc_writer *b, uint8_t byte)
{
    if (b->len == b->cap) {
        size_t cap = b->cap ? 2 * b->cap : 256;
        uint8_t *p = realloc(b->buf, cap);
        if (!p) {
            b->nomem = 1;
            return;
        }
        b->buf = p;
        b->cap = cap;
    }
    b->buf[b->len++] = byte;
}

void nrc_write(nrc_writer *b, uint64_t v, unsigned width)
{
    if (width == 0 || b->nomem)
        return;
    v &= (1ull << width) - 1;                  /* width ≤ 33 */
    b->acc = (b->acc << width) | v;
    b->nbits += width;
    while (b->nbits >= 8) {
        b->nbits -= 8;
        bw_put(b, (uint8_t)(b->acc >> b->nbits));
    }
    b->acc &= (1ull << b->nbits) - 1;
}

static void bw_unary(nrc_writer *b, uint64_t q)
{
    while (q > 0) {
        unsigned n = q < 32 ? (unsigned)q : 32;
        nrc_write(b, (1ull << n) - 1, n);
        q -= n;
    }
    nrc_write(b, 0, 1);
}

static unsigned bitlen(uint64_t m)
{
    unsigned l = 0;
    while (m) {
        l++;
        m >>= 1;
    }
    return l;
}

void nrc_gamma(nrc_writer *b, uint64_t m)
{
    unsigned l = bitlen(m) - 1;
    nrc_write(b, 0, l);
    nrc_write(b, m, l + 1);
}

void nrc_flush(nrc_writer *b)
{
    if (b->nbits) {
        bw_put(b, (uint8_t)(b->acc << (8 - b->nbits)));
        b->acc = 0;
        b->nbits = 0;
    }
}

uint64_t nrc_read(nrc_reader *r, unsigned width)
{
    uint64_t v = 0;
    if (width == 0 || r->bad)
        return 0;
    if (r->pos + width > r->end) {
        r->bad = 1;
        return 0;
    }
    for (unsigned i = 0; i < width; i++, r->pos++)
        v = (v << 1) | ((r->d[r->pos >> 3] >> (7 - (r->pos & 7))) & 1u);
    return v;
}

static uint64_t br_unary(nrc_reader *r)
{
    uint64_t q = 0;
    while (!r->bad && nrc_read(r, 1))
        q++;
    return q;
}

uint64_t nrc_read_gamma(nrc_reader *r)
{
    unsigned l = 0;
    while (!r->bad && nrc_read(r, 1) == 0) {
        if (r->bad)
            return 0;
        if (++l > 32) {
            r->bad = 1;
            return 0;
        }
    }
    if (r->bad)
        return 0;
    return (1ull << l) | nrc_read(r, l);
}

/* ------------------------------------------------------------------ small arithmetic */

static uint64_t mask_of(unsigned w) { return (w >= 64) ? ~0ull : (1ull << w) - 1; }

static int64_t to_signed(uint64_t v, unsigned w)
{
    return (v & (1ull << (w - 1))) ? (int64_t)v - (int64_t)(1ull << w) : (int64_t)v;
}

static int64_t value(uint64_t x, unsigned w, int sgn) { return sgn ? to_signed(x, w) : (int64_t)x; }

static int64_t floor_shift(int64_t a, unsigned s)  /* a >> s, arithmetic, in portable C */
{
    return a >= 0 ? (int64_t)((uint64_t)a >> s) : (int64_t) ~(((uint64_t) ~a) >> s);
}

static uint64_t fold(uint64_t r, unsigned w)
{
    uint64_t v = r & mask_of(w);
    if (v & (1ull << (w - 1)))
        return 2 * ((mask_of(w) - v) + 1) - 1;
    return 2 * v;
}

static uint64_t unfold(uint64_t u) { return (u & 1) ? ~(u >> 1) : (u >> 1); }

/* An exact sum of products, as two magnitudes — the autocorrelation needs up to ~2^80. */
typedef struct {
    uint64_t hi, lo;
} u128;

static void u128_add(u128 *a, uint64_t m)
{
    a->lo += m;
    if (a->lo < m)
        a->hi++;
}

static double u128_to_double(u128 m)       /* round to nearest, ties to even, as Python's float() */
{
    if (m.hi == 0 && m.lo < (1ull << 53))
        return (double)m.lo;
    unsigned l = m.hi ? 64 + bitlen(m.hi) : bitlen(m.lo);
    unsigned sh = l - 53;                    /* 1 .. 75 */
    uint64_t mant, rem_hi, rem_lo, half_hi, half_lo;
    if (sh >= 64) {
        mant = m.hi >> (sh - 64);
        rem_hi = m.hi & mask_of(sh - 64);
        rem_lo = m.lo;
        half_hi = (sh - 64) ? 1ull << (sh - 65) : 0;
        half_lo = (sh - 64) ? 0 : 1ull << 63;
    } else {
        mant = (m.lo >> sh) | (m.hi << (64 - sh));
        rem_hi = 0;
        rem_lo = m.lo & mask_of(sh);
        half_hi = 0;
        half_lo = 1ull << (sh - 1);
    }
    int up;
    if (rem_hi != half_hi)
        up = rem_hi > half_hi;
    else if (rem_lo != half_lo)
        up = rem_lo > half_lo;
    else
        up = (int)(mant & 1);
    if (up && ++mant == (1ull << 53)) {
        mant >>= 1;
        sh++;
    }
    return ldexp((double)mant, (int)sh);
}

/* ------------------------------------------------------------------ residuals */

static uint64_t part_bits(const uint64_t *us, unsigned m, unsigned k, unsigned w)
{
    uint64_t t = 5;
    if (k == 31)
        return t + (uint64_t)w * m;
    for (unsigned i = 0; i < m; i++)
        t += (us[i] >> k) + 1 + k;
    return t;
}

static unsigned best_k(const uint64_t *us, unsigned m, unsigned w, uint64_t *bits)
{
    unsigned kmax = w < 30 ? w : 30, bk = 0;
    uint64_t best = UINT64_MAX;
    for (unsigned k = 0; k <= kmax; k++) {
        uint64_t b = part_bits(us, m, k, w);
        if (b < best) {
            best = b;
            bk = k;
        }
    }
    uint64_t esc = part_bits(us, m, 31, w);
    if (esc <= best) {
        *bits = esc;
        return 31;
    }
    *bits = best;
    return bk;
}

static uint64_t residual_bits(const uint32_t *rs, size_t n, unsigned w)
{
    uint64_t total = 0, b;
    uint64_t us[32];
    for (size_t i = 0; i < n; i += 32) {
        unsigned m = (unsigned)(n - i < 32 ? n - i : 32);
        for (unsigned j = 0; j < m; j++)
            us[j] = fold(rs[i + j], w);
        best_k(us, m, w, &b);
        total += b;
    }
    return total;
}

static void write_residuals(nrc_writer *bw, const uint32_t *rs, size_t n, unsigned w)
{
    uint64_t us[32], b;
    for (size_t i = 0; i < n; i += 32) {
        unsigned m = (unsigned)(n - i < 32 ? n - i : 32);
        for (unsigned j = 0; j < m; j++)
            us[j] = fold(rs[i + j], w);
        unsigned k = best_k(us, m, w, &b);
        nrc_write(bw, k, 5);
        for (unsigned j = 0; j < m; j++) {
            if (k == 31) {
                nrc_write(bw, us[j], w);
            } else {
                bw_unary(bw, us[j] >> k);
                nrc_write(bw, us[j], k);
            }
        }
    }
}

static int read_residual(nrc_reader *r, unsigned *k, size_t j, unsigned w, uint64_t *res)
{
    if (j % 32 == 0)
        *k = (unsigned)nrc_read(r, 5);
    uint64_t u;
    if (*k == 31) {
        u = nrc_read(r, w);
    } else {
        uint64_t q = br_unary(r);
        u = (q << *k) | nrc_read(r, *k);
    }
    *res = unfold(u);
    return r->bad;
}

/* ------------------------------------------------------------------ the predictor */

static int levinson(const double *r, int max_order, double sets[MAX_ORDER][MAX_ORDER])
{
    double a[MAX_ORDER], nw[MAX_ORDER], err;
    int nsets = 0;
    if (r[0] <= 0.0)
        return 0;
    err = r[0];
    for (int p = 1; p <= max_order; p++) {
        double acc = r[p];
        for (int j = 1; j < p; j++)
            acc = acc - a[j - 1] * r[p - j];
        if (err <= 0.0)
            break;
        double k = acc / err;
        if (!(-1.0 < k && k < 1.0))
            break;
        for (int j = 1; j < p; j++)
            nw[j - 1] = a[j - 1] - k * a[p - j - 1];
        nw[p - 1] = k;
        memcpy(a, nw, sizeof(double) * (size_t)p);
        err = err * (1.0 - k * k);
        memcpy(sets[nsets++], a, sizeof(double) * (size_t)p);
    }
    return nsets;
}

static int quantise(const double *a, int p, int *s_out, int32_t *q)
{
    double amax = 0.0;
    for (int j = 0; j < p; j++)
        if (fabs(a[j]) > amax)
            amax = fabs(a[j]);
    if (amax == 0.0) {
        *s_out = 15;
        for (int j = 0; j < p; j++)
            q[j] = 0;
        return 1;
    }
    int s = 15;
    while (s >= 0 && amax * (double)(1 << s) >= (double)(1 << 14))
        s--;
    if (s < 0)
        return 0;
    for (int j = 0; j < p; j++) {
        double v = a[j] * (double)(1 << s);
        int64_t qi = (int64_t)(fabs(v) + 0.5);
        if (qi >= (1 << 14))
            return 0;
        q[j] = (int32_t)(v >= 0 ? qi : -qi);
    }
    *s_out = s;
    return 1;
}

static void lpc_residuals(const uint32_t *xs, size_t n, unsigned w, int sgn, int p, int s,
                          const int32_t *c, uint32_t *rs)
{
    uint64_t mask = mask_of(w);
    for (size_t i = (size_t)p; i < n; i++) {
        int64_t acc = 0;
        for (int j = 1; j <= p; j++)
            acc += (int64_t)c[j - 1] * value(xs[i - (size_t)j], w, sgn);
        rs[i - (size_t)p] = (uint32_t)(((uint64_t)value(xs[i], w, sgn)
                                        - (uint64_t)floor_shift(acc, (unsigned)s)) & mask);
    }
}

/* ------------------------------------------------------------------ a series */

typedef struct {
    uint64_t bits;
    int use_ref, mode, order, s;
    int32_t c[MAX_ORDER];
    int set;
} choice_t;

static int better(const choice_t *b, uint64_t bits, int use_ref, int mode, int order)
{
    if (!b->set) return 1;
    if (bits != b->bits) return bits < b->bits;
    if (use_ref != b->use_ref) return use_ref < b->use_ref;
    if (mode != b->mode) return mode < b->mode;
    return order < b->order;
}

static void take(choice_t *b, uint64_t bits, int use_ref, int mode, int order, int s,
                 const int32_t *c)
{
    b->bits = bits;
    b->use_ref = use_ref;
    b->mode = mode;
    b->order = order;
    b->s = s;
    if (c)
        memcpy(b->c, c, sizeof(int32_t) * (size_t)order);
    b->set = 1;
}

static void diff1(const uint32_t *xs, size_t n, uint64_t mask, uint32_t *rs)
{
    for (size_t i = 1; i < n; i++)
        rs[i - 1] = (uint32_t)(((uint64_t)xs[i] - xs[i - 1]) & mask);
}

static void diff2(const uint32_t *xs, size_t n, uint64_t mask, uint32_t *rs)
{
    for (size_t i = 2; i < n; i++)
        rs[i - 2] = (uint32_t)(((uint64_t)xs[i] - 2 * (uint64_t)xs[i - 1] + xs[i - 2]) & mask);
}

static void consider(const uint32_t *xs, size_t n, unsigned w, int sgn, int use_ref,
                     uint32_t *rs, choice_t *best)
{
    uint64_t mask = mask_of(w), bits;
    size_t i;
    for (i = 1; i < n && xs[i] == xs[0]; i++)
        ;
    if (i == n && better(best, 3 + w, use_ref, 0, 0))
        take(best, 3 + w, use_ref, 0, 0, 0, NULL);
    diff1(xs, n, mask, rs);
    bits = 3 + w + residual_bits(rs, n - 1, w);
    if (better(best, bits, use_ref, 1, 0))
        take(best, bits, use_ref, 1, 0, 0, NULL);
    if (n >= 2) {
        diff2(xs, n, mask, rs);
        bits = 3 + 2 * (uint64_t)w + residual_bits(rs, n - 2, w);
        if (better(best, bits, use_ref, 2, 0))
            take(best, bits, use_ref, 2, 0, 0, NULL);
    }
    /* LPC: the autocorrelation exact, then Levinson–Durbin in double */
    double r[MAX_ORDER + 1];
    int nr = 0;
    for (size_t lag = 0; lag <= MAX_ORDER && lag < n; lag++, nr++) {
        u128 pos = {0, 0}, neg = {0, 0};
        for (i = lag; i < n; i++) {
            int64_t a = value(xs[i], w, sgn), b = value(xs[i - lag], w, sgn);
            uint64_t ma = (uint64_t)(a < 0 ? -a : a), mb = (uint64_t)(b < 0 ? -b : b);
            u128_add(((a < 0) != (b < 0)) ? &neg : &pos, ma * mb);
        }
        int negative = pos.hi < neg.hi || (pos.hi == neg.hi && pos.lo < neg.lo);
        u128 big = negative ? neg : pos, small = negative ? pos : neg, d;
        d.lo = big.lo - small.lo;
        d.hi = big.hi - small.hi - (big.lo < small.lo);
        r[lag] = negative ? -u128_to_double(d) : u128_to_double(d);
    }
    if (nr >= 2) {
        double sets[MAX_ORDER][MAX_ORDER];
        int ns = levinson(r, nr - 1 < MAX_ORDER ? nr - 1 : MAX_ORDER, sets);
        for (int k = 0; k < ns; k++) {
            int p = k + 1, s;
            int32_t c[MAX_ORDER];
            if (!quantise(sets[k], p, &s, c) || (size_t)p >= n)
                continue;
            lpc_residuals(xs, n, w, sgn, p, s, c, rs);
            bits = 3 + 3 + 4 + 15 * (uint64_t)p + (uint64_t)p * w + residual_bits(rs, n - (size_t)p, w);
            if (better(best, bits, use_ref, 3, p))
                take(best, bits, use_ref, 3, p, s, c);
        }
    }
    bits = 3 + (uint64_t)n * w;
    if (better(best, bits, use_ref, 4, 0))
        take(best, bits, use_ref, 4, 0, 0, NULL);
}

int nrc_write_series(nrc_writer *bw, const uint32_t *xs, size_t n, unsigned w, int sgn,
                        const uint32_t *ref)
{
    if (n == 0)
        return NRC_OK;
    uint64_t mask = mask_of(w);
    uint32_t *rs = malloc(sizeof(uint32_t) * n), *dv = NULL;
    if (!rs)
        return NRC_ENOMEM;
    choice_t best;
    memset(&best, 0, sizeof best);
    consider(xs, n, w, sgn, 0, rs, &best);
    if (ref) {
        dv = malloc(sizeof(uint32_t) * n);
        if (!dv) {
            free(rs);
            return NRC_ENOMEM;
        }
        for (size_t i = 0; i < n; i++)
            dv[i] = (uint32_t)(((uint64_t)xs[i] - ref[i]) & mask);
        consider(dv, n, w, sgn, 1, rs, &best);
    }
    const uint32_t *v = best.use_ref ? dv : xs;
    if (ref)
        nrc_write(bw, (uint64_t)best.use_ref, 1);
    nrc_write(bw, (uint64_t)best.mode, 3);
    switch (best.mode) {
    case 0:
        nrc_write(bw, v[0], w);
        break;
    case 1:
        nrc_write(bw, v[0], w);
        diff1(v, n, mask, rs);
        write_residuals(bw, rs, n - 1, w);
        break;
    case 2:
        nrc_write(bw, v[0], w);
        nrc_write(bw, v[1], w);
        diff2(v, n, mask, rs);
        write_residuals(bw, rs, n - 2, w);
        break;
    case 3:
        nrc_write(bw, (uint64_t)(best.order - 1), 3);
        nrc_write(bw, (uint64_t)best.s, 4);
        for (int j = 0; j < best.order; j++)
            nrc_write(bw, (uint64_t)best.c[j] & 0x7FFF, 15);
        for (int j = 0; j < best.order; j++)
            nrc_write(bw, v[j], w);
        lpc_residuals(v, n, w, sgn, best.order, best.s, best.c, rs);
        write_residuals(bw, rs, n - (size_t)best.order, w);
        break;
    default:
        for (size_t i = 0; i < n; i++)
            nrc_write(bw, v[i], w);
    }
    free(rs);
    free(dv);
    return bw->nomem ? NRC_ENOMEM : NRC_OK;
}

int nrc_read_series(nrc_reader *r, size_t n, unsigned w, int sgn, const uint32_t *ref, uint32_t *xs)
{
    if (n == 0)
        return NRC_OK;
    uint64_t mask = mask_of(w), res;
    unsigned k = 0;
    int use_ref = ref ? (int)nrc_read(r, 1) : 0;
    int mode = (int)nrc_read(r, 3);
    if (r->bad)
        return NRC_DAMAGED;
    switch (mode) {
    case 0: {
        uint32_t v = (uint32_t)nrc_read(r, w);
        for (size_t i = 0; i < n; i++)
            xs[i] = v;
        break;
    }
    case 1:
        xs[0] = (uint32_t)nrc_read(r, w);
        for (size_t i = 1; i < n; i++) {
            if (read_residual(r, &k, i - 1, w, &res))
                return NRC_DAMAGED;
            xs[i] = (uint32_t)((xs[i - 1] + res) & mask);
        }
        break;
    case 2:
        if (n < 2)
            return NRC_DAMAGED;
        xs[0] = (uint32_t)nrc_read(r, w);
        xs[1] = (uint32_t)nrc_read(r, w);
        for (size_t i = 2; i < n; i++) {
            if (read_residual(r, &k, i - 2, w, &res))
                return NRC_DAMAGED;
            xs[i] = (uint32_t)((res + 2 * (uint64_t)xs[i - 1] - xs[i - 2]) & mask);
        }
        break;
    case 3: {
        size_t p = (size_t)nrc_read(r, 3) + 1;
        if (r->bad || p >= n)
            return NRC_DAMAGED;
        unsigned s = (unsigned)nrc_read(r, 4);
        int64_t c[MAX_ORDER];
        for (size_t j = 0; j < p; j++)
            c[j] = to_signed(nrc_read(r, 15), 15);
        for (size_t j = 0; j < p; j++)
            xs[j] = (uint32_t)nrc_read(r, w);
        for (size_t i = p; i < n; i++) {
            if (read_residual(r, &k, i - p, w, &res))
                return NRC_DAMAGED;
            int64_t acc = 0;
            for (size_t j = 1; j <= p; j++)
                acc += c[j - 1] * value(xs[i - j], w, sgn);
            xs[i] = (uint32_t)((res + (uint64_t)floor_shift(acc, s)) & mask);
        }
        break;
    }
    case 4:
        for (size_t i = 0; i < n && !r->bad; i++)
            xs[i] = (uint32_t)nrc_read(r, w);
        break;
    default:
        return NRC_DAMAGED;
    }
    if (r->bad)
        return NRC_DAMAGED;
    if (use_ref)
        for (size_t i = 0; i < n; i++)
            xs[i] = (uint32_t)(((uint64_t)xs[i] + ref[i]) & mask);
    return NRC_OK;
}


void nrc_reader_init(nrc_reader *r, const uint8_t *d, size_t len)
{
    r->d = d;
    r->pos = 0;
    r->end = (uint64_t)len * 8;
    r->bad = 0;
}

int nrc_finish(const nrc_reader *r)
{
    return (r->bad || r->end - r->pos >= 8) ? NRC_DAMAGED : NRC_OK;
}
