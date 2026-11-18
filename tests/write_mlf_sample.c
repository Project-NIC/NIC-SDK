/* A sample MLF file whose segments carry NRC streams, written by the C cores; tests/test_mlf.py
 * writes the same with the Python and compares the bytes. The series are made by an LCG both
 * sides share: a random walk, a smooth curve, a pair coded against its reference, a constant and
 * noise. */
#include "mlf.h"
#include "nrc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t seed = 1;
static uint32_t lcg(void) { seed = seed * 1103515245u + 12345u; return (seed >> 8) & 0xFFFFu; }

#define N 600

static int body(uint32_t seg, uint8_t **out, size_t *len, uint8_t *codec)
{
    static uint32_t a[N], b[N], c[N], d[N], e[N];
    int32_t walk = 0, v = 0, s = 0;
    for (int i = 0; i < N; i++) {
        walk += (int32_t)(lcg() % 21) - 10;
        v += (int32_t)(lcg() % 5) - 2;
        s += v;
        a[i] = (uint32_t)walk & 0xFFFFu;
        b[i] = (uint32_t)s;
        c[i] = (uint32_t)(walk + (int32_t)(lcg() % 3) - 1) & 0xFFFFu;
        d[i] = seg & 0xFFu;
        e[i] = lcg() & 0xFFFu;
    }
    nrc_writer w;
    memset(&w, 0, sizeof w);
    nrc_gamma(&w, N);
    int err = nrc_write_series(&w, a, N, 16, 1, NULL);
    err |= nrc_write_series(&w, b, N, 32, 1, NULL);
    err |= nrc_write_series(&w, c, N, 16, 1, a);
    err |= nrc_write_series(&w, d, N, 8, 0, NULL);
    err |= nrc_write_series(&w, e, N, 12, 0, NULL);
    nrc_flush(&w);
    if (err || w.nomem)
        return 1;
    size_t rawlen = (size_t)N * 4 * 5;            /* the raw body: every value as a u32 */
    if (w.len < rawlen) {
        *out = w.buf; *len = w.len; *codec = MLF_NRC;
        return 0;
    }
    uint8_t *r = malloc(rawlen);
    const uint32_t *cols[5] = {a, b, c, d, e};
    for (int k = 0; k < 5; k++)
        for (int i = 0; i < N; i++)
            for (int j = 0; j < 4; j++)
                r[(k * N + i) * 4 + j] = (uint8_t)(cols[k][i] >> (8 * j));
    free(w.buf);
    *out = r; *len = rawlen; *codec = MLF_RAW;
    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 2)
        return 2;
    static const uint8_t sections[] = {1, 0, 0, 0, 7, 0, 0, 0, 'p', 'r', 'o', 'f', 'i', 'l', 'e'};
    static const uint8_t change[] = {2, 0, 0, 0, 3, 0, 0, 0, 'n', 'e', 'w'};
    mlf_header h = {7, 1000000, 0x01020304u, 0, 0, sections, sizeof sections};
    mlf_writer w;
    if (mlf_writer_open(&w, &h))
        return 1;
    for (uint32_t i = 0; i < 20; i++) {
        uint8_t *b, *seg, codec;
        size_t blen, slen;
        if (i == 10 && mlf_writer_config(&w, 1000000 + 8 * i, change, sizeof change))
            return 1;
        if (body(i, &b, &blen, &codec)
            || mlf_segment_build(1000000 + 8 * i, 1000000 + 8 * i + 7, codec, b, blen, &seg, &slen)
            || mlf_writer_append(&w, seg, slen, MLF_SEG, 1000000 + 8 * i + 7))
            return 1;
        free(b);
        free(seg);
    }
    FILE *f = fopen(argv[1], "wb");
    if (!f || fwrite(w.image, 1, w.size, f) != w.size)
        return 1;
    fclose(f);
    mlf_writer_close(&w);
    return 0;
}
