/* MLF — the C reference of its core; see mlf.h. */

#include "mlf.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ little-endian */

static void put16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { put16(p, v & 0xFFFF); put16(p + 2, v >> 16); }
static uint32_t get16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t get32(const uint8_t *p) { return get16(p) | get16(p + 2) << 16; }

/* ------------------------------------------------------------------ checks and framing */

uint32_t mlf_crc32(const uint8_t *b, size_t n)
{
    static uint32_t table[256];
    static int ready;
    if (!ready) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++)
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        ready = 1;
    }
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++)
        c = table[(c ^ b[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

uint16_t mlf_crc16(const uint8_t *b, size_t n)
{
    uint16_t c = 0xFFFF;
    for (size_t i = 0; i < n; i++) {
        c ^= (uint16_t)(b[i] << 8);
        for (int k = 0; k < 8; k++)
            c = (c & 0x8000) ? (uint16_t)((c << 1) ^ 0x1021) : (uint16_t)(c << 1);
    }
    return c;
}

size_t mlf_cobs_encode(const uint8_t *in, size_t n, uint8_t *out)
{
    size_t at = 0, o = 1;
    uint8_t code = 1;
    for (size_t i = 0; i < n; i++) {
        if (in[i] == 0) {
            out[at] = code;
            at = o++;
            code = 1;
        } else {
            out[o++] = in[i];
            if (++code == 0xFF) {
                out[at] = code;
                at = o++;
                code = 1;
            }
        }
    }
    out[at] = code;
    return o;
}

int mlf_cobs_decode(const uint8_t *in, size_t n, uint8_t *out, size_t *outlen)
{
    size_t i = 0, o = 0;
    while (i < n) {
        uint8_t c = in[i];
        if (c == 0 || i + c > n)
            return MLF_DAMAGED;
        for (size_t k = 1; k < c; k++) {
            if (in[i + k] == 0)
                return MLF_DAMAGED;
            out[o++] = in[i + k];
        }
        i += c;
        if (c < 0xFF && i < n)
            out[o++] = 0;
    }
    *outlen = o;
    return MLF_OK;
}

/* ------------------------------------------------------------------ sections */

int mlf_sections_parse(const uint8_t *b, size_t len, mlf_sections *out)
{
    size_t i = 0;
    out->n = 0;
    while (i < len) {
        if (len - i < 8)
            return MLF_DAMAGED;
        uint32_t ln = get32(b + i + 4);
        if (ln > len - i - 8)
            return MLF_DAMAGED;
        if (out->n == MLF_MAX_SECTIONS)
            return MLF_EINVAL;
        out->s[out->n].id = b[i];
        out->s[out->n].length = ln;
        out->s[out->n].body = b + i + 8;
        out->n++;
        i += 8 + ln;
    }
    return MLF_OK;
}

int mlf_sections_apply(mlf_sections *f, const mlf_sections *c)
{
    size_t m = 0;
    for (size_t i = 0; i < f->n; i++) {
        int replaced = 0;
        for (size_t j = 0; j < c->n; j++)
            replaced |= f->s[i].id == c->s[j].id;
        if (replaced)
            free((void *)f->s[i].body);
        else
            f->s[m++] = f->s[i];
    }
    f->n = m;
    for (size_t j = 0; j < c->n; j++) {
        if (f->n == MLF_MAX_SECTIONS)
            return MLF_EINVAL;
        uint8_t *b = malloc(c->s[j].length ? c->s[j].length : 1);
        if (!b)
            return MLF_ENOMEM;
        memcpy(b, c->s[j].body, c->s[j].length);
        f->s[f->n] = c->s[j];
        f->s[f->n].body = b;
        f->n++;
    }
    return MLF_OK;
}

void mlf_sections_free(mlf_sections *s)
{
    for (size_t i = 0; i < s->n; i++)
        free((void *)s->s[i].body);
    s->n = 0;
}

/* ------------------------------------------------------------------ the header */

size_t mlf_padded(size_t length)
{
    return (length + MLF_SECTOR - 1) / MLF_SECTOR * MLF_SECTOR;
}

size_t mlf_header_build(const mlf_header *h, uint8_t *out)
{
    size_t length = 20 + h->sections_len + 4;
    memcpy(out, "MLF", 3);
    out[3] = (uint8_t)(h->letter ? h->letter : 'U');
    out[4] = MLF_FORMAT;
    out[5] = h->size;
    put16(out + 6, h->sequence);
    put32(out + 8, (uint32_t)length);
    put32(out + 12, h->first_second);
    put32(out + 16, h->source);
    memcpy(out + 20, h->sections, h->sections_len);
    put32(out + 20 + h->sections_len, mlf_crc32(out, 20 + h->sections_len));
    return length;
}

int mlf_header_parse(const uint8_t *b, size_t len, mlf_header *h)
{
    if (len < 24 || memcmp(b, "MLF", 3) || b[3] < 0x21 || b[3] > 0x7E || b[4] != MLF_FORMAT)
        return MLF_DAMAGED;
    uint32_t length = get32(b + 8);
    if (length < 24 || length > len || get32(b + length - 4) != mlf_crc32(b, length - 4))
        return MLF_DAMAGED;
    if (b[5] > 7)
        return MLF_DAMAGED;
    mlf_sections tmp;
    if (mlf_sections_parse(b + 20, length - 24, &tmp))
        return MLF_DAMAGED;
    h->letter = (char)b[3];
    h->size = b[5];
    h->sequence = (uint16_t)get16(b + 6);
    h->length = length;
    h->first_second = get32(b + 12);
    h->source = get32(b + 16);
    h->sections = b + 20;
    h->sections_len = length - 24;
    return MLF_OK;
}

/* ------------------------------------------------------------------ segments and configs */

static int segment(uint32_t first, uint32_t last, uint8_t codec, uint8_t run, uint16_t place,
                   const uint8_t *body, size_t blen, uint8_t **out, size_t *len);

int mlf_segment_build(uint32_t first, uint32_t last, uint8_t codec, const uint8_t *body, size_t blen,
                      uint8_t **out, size_t *len)
{
    return segment(first, last, codec, 0, 0, body, blen, out, len);
}

int mlf_run_build(uint32_t first, uint32_t last, uint8_t codec, uint16_t place, const uint8_t *body,
                  size_t blen, uint8_t **out, size_t *len)
{
    return segment(first, last, codec, 1, place, body, blen, out, len);
}

static int segment(uint32_t first, uint32_t last, uint8_t codec, uint8_t run, uint16_t place,
                   const uint8_t *body, size_t blen, uint8_t **out, size_t *len)
{
    size_t n = 20 + blen + 4;
    uint8_t *b = malloc(n);
    if (!b)
        return MLF_ENOMEM;
    memcpy(b, "MSEG", 4);
    put32(b + 4, (uint32_t)n);
    put32(b + 8, first);
    put32(b + 12, last);
    b[16] = codec;
    b[17] = run;
    put16(b + 18, place);
    memcpy(b + 20, body, blen);
    put32(b + 20 + blen, mlf_crc32(b, 20 + blen));
    *out = b;
    *len = n;
    return MLF_OK;
}

int mlf_config_build(uint32_t from_second, const uint8_t *sections, size_t slen, uint8_t **out,
                     size_t *len)
{
    size_t n = 16 + slen + 4;
    uint8_t *b = malloc(n);
    if (!b)
        return MLF_ENOMEM;
    memcpy(b, "MCFG", 4);
    put32(b + 4, (uint32_t)n);
    put32(b + 8, from_second);
    put32(b + 12, 0);
    memcpy(b + 16, sections, slen);
    put32(b + 16 + slen, mlf_crc32(b, 16 + slen));
    *out = b;
    *len = n;
    return MLF_OK;
}

void mlf_entry_build(const mlf_entry *e, uint8_t out[16])
{
    put32(out, e->second);
    put32(out + 4, e->offset);
    put32(out + 8, e->length);
    out[12] = e->type;
    out[13] = 0;
    put16(out + 14, mlf_crc16(out, 14));
}

int mlf_entry_parse(const uint8_t b[16], mlf_entry *e)
{
    if (get16(b + 14) != mlf_crc16(b, 14) || (b[12] != MLF_SEG && b[12] != MLF_CFG))
        return MLF_DAMAGED;
    e->second = get32(b);
    e->offset = get32(b + 4);
    e->length = get32(b + 8);
    e->type = b[12];
    return MLF_OK;
}

int mlf_object_parse(const uint8_t *b, size_t len, mlf_object *o)
{
    if (len < 20)
        return MLF_DAMAGED;
    int seg = !memcmp(b, "MSEG", 4), cfg = !memcmp(b, "MCFG", 4);
    if (!seg && !cfg)
        return MLF_DAMAGED;
    uint32_t length = get32(b + 4);
    if (length != len || get32(b + length - 4) != mlf_crc32(b, length - 4))
        return MLF_DAMAGED;
    memset(o, 0, sizeof *o);
    if (seg) {
        if (length < 24)
            return MLF_DAMAGED;
        o->type = MLF_SEG;
        o->first = get32(b + 8);
        o->last = get32(b + 12);
        o->codec = b[16];
        o->run = b[17] == 1;
        o->place = o->run ? (uint16_t)get16(b + 18) : 0;
        o->body = b + 20;
        o->body_len = length - 24;
        return MLF_OK;
    }
    mlf_sections tmp;
    o->type = MLF_CFG;
    o->from = get32(b + 8);
    o->body = b + 16;
    o->body_len = length - 20;
    return mlf_sections_parse(o->body, o->body_len, &tmp) ? MLF_DAMAGED : MLF_OK;
}

/* ------------------------------------------------------------------ a file in memory */

int mlf_writer_open(mlf_writer *w, const mlf_header *h)
{
    mlf_sections hs;
    memset(w, 0, sizeof *w);
    if (h->size > 7 || 24 + h->sections_len > ((size_t)MLF_MB << h->size)
        || mlf_sections_parse(h->sections, h->sections_len, &hs))
        return MLF_EINVAL;
    w->size = (size_t)MLF_MB << h->size;
    w->image = calloc(1, w->size);
    if (!w->image)
        return MLF_ENOMEM;
    size_t hl = mlf_header_build(h, w->image);
    w->pad = mlf_padded(hl);
    if (2 * w->pad > w->size) {
        free(w->image);
        w->image = NULL;
        return MLF_EINVAL;
    }
    memcpy(w->image + w->size - w->pad, w->image, hl);      /* the mirror */
    w->end = w->pad;
    return mlf_sections_apply(&w->in_force, &hs);
}

int mlf_writer_append(mlf_writer *w, const uint8_t *obj, size_t len, uint8_t type, uint32_t second)
{
    if (second < w->last)
        return MLF_EINVAL;
    size_t desc = w->size - w->pad - 16 * (w->entries + 1);
    uint8_t *f = malloc(len + len / 254 + 2);
    if (!f)
        return MLF_ENOMEM;
    size_t flen = mlf_cobs_encode(obj, len, f);
    f[flen++] = 0;
    if (w->end + flen > desc) {
        free(f);
        return MLF_FULL;
    }
    memcpy(w->image + w->end, f, flen);
    mlf_entry e = {second, (uint32_t)w->end, (uint32_t)(flen - 1), type};
    mlf_entry_build(&e, w->image + desc);
    w->end += flen;
    w->entries++;
    w->last = second;
    free(f);
    return MLF_OK;
}

int mlf_writer_config(mlf_writer *w, uint32_t from_second, const uint8_t *sections, size_t slen)
{
    mlf_sections cs;
    uint8_t *cfg;
    size_t len;
    int e = mlf_sections_parse(sections, slen, &cs);
    if (e)
        return MLF_EINVAL;
    if ((e = mlf_config_build(from_second, sections, slen, &cfg, &len)))
        return e;
    e = mlf_writer_append(w, cfg, len, MLF_CFG, from_second);
    free(cfg);
    return e ? e : mlf_sections_apply(&w->in_force, &cs);
}

void mlf_writer_close(mlf_writer *w)
{
    free(w->image);
    mlf_sections_free(&w->in_force);
    memset(w, 0, sizeof *w);
}

int mlf_reader_open(mlf_reader *r, const uint8_t *image, size_t len)
{
    memset(r, 0, sizeof *r);
    if (mlf_header_parse(image, len, &r->h)) {
        /* the header failed: the fewest sectors k at the end that begin with the mark, whose
         * length fits in k sectors and whose CRC holds */
        int found = 0;
        for (size_t k = 1; !found && 2 * k * MLF_SECTOR <= len; k++) {
            size_t at = len - k * MLF_SECTOR;
            found = !memcmp(image + at, "MLF", 3) && !mlf_header_parse(image + at, len - at, &r->h)
                    && mlf_padded(r->h.length) == k * MLF_SECTOR;
        }
        if (!found)
            return MLF_DAMAGED;
    }
    if (len != ((size_t)MLF_MB << r->h.size))
        return MLF_DAMAGED;
    r->image = image;
    r->size = len;
    r->pad = mlf_padded(r->h.length);
    size_t lo = 0, hi = (len - 2 * r->pad) / 16;
    while (lo < hi) {           /* a binary search for the last good entry */
        size_t mid = lo + (hi - lo) / 2;
        mlf_entry e;
        if (mlf_entry_parse(image + len - r->pad - 16 * (mid + 1), &e))
            hi = mid;
        else
            lo = mid + 1;
    }
    r->entries = lo;
    return MLF_OK;
}

int mlf_reader_entry(const mlf_reader *r, size_t i, mlf_entry *e)
{
    if (i >= r->entries)
        return MLF_EINVAL;
    return mlf_entry_parse(r->image + r->size - r->pad - 16 * (i + 1), e);
}

static uint32_t second_at(const mlf_reader *r, size_t i)
{
    mlf_entry e;
    for (i++; i-- > 0;)         /* a damaged entry takes the last good one's second, 0 with none */
        if (!mlf_reader_entry(r, i, &e))
            return e.second;
    return 0;
}

size_t mlf_reader_find(const mlf_reader *r, uint32_t second)
{
    size_t lo = 0, hi = r->entries;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (second_at(r, mid) < second)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

int mlf_reader_object(const mlf_reader *r, size_t i, uint8_t **buf, mlf_object *o)
{
    mlf_entry e;
    size_t len;
    *buf = NULL;
    if (mlf_reader_entry(r, i, &e))
        return MLF_DAMAGED;
    if ((uint64_t)e.offset + e.length >= r->size || r->image[e.offset + e.length] != 0)
        return MLF_DAMAGED;
    *buf = malloc(e.length ? e.length : 1);
    if (!*buf)
        return MLF_ENOMEM;
    if (mlf_cobs_decode(r->image + e.offset, e.length, *buf, &len)
        || mlf_object_parse(*buf, len, o) || o->type != e.type)
        return MLF_DAMAGED;
    return MLF_OK;
}

