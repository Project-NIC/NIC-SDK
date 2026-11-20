/* NIC-SDK — MUF's reader. */
#include <math.h>
#include <string.h>
#include "muf_read.h"

static uint16_t g16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t g32(const uint8_t *p) { return (uint32_t)g16(p) | (uint32_t)g16(p + 2) << 16; }
static uint64_t g64(const uint8_t *p) { return (uint64_t)g32(p) | (uint64_t)g32(p + 4) << 32; }

static uint32_t crc32(const uint8_t *d, unsigned n)
{
	uint32_t c = 0xFFFFFFFFu;

	while (n--) {
		c ^= *d++;
		for (int k = 0; k < 8; k++) {
			c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
		}
	}
	return ~c;
}

static int good(const uint8_t *b) { return crc32(b, 508) == g32(b + 508); }

static unsigned type_size(char t)
{
	switch (t) {
	case 'b': case 'B': return 1;
	case 'h': case 'H': case 'e': return 2;
	case 'i': case 'I': case 'f': return 4;
	case 'q': case 'Q': case 'd': return 8;
	default: return 0;
	}
}

static void get_group(struct muf_group *g, const uint8_t *p)
{
	g->stream = p[0];
	g->first = p[1];
	g->count = p[2];
	g->type = (char)p[3];
	g->flags = p[4];
	g->exp10 = (int8_t)p[5];
	g->mantissa = (int16_t)g16(p + 6);
	g->add = (int32_t)g32(p + 8);
	memcpy(g->name, p + 12, 8);
	g->name[8] = '\0';
	memcpy(g->unit, p + 20, 8);
	g->unit[8] = '\0';
	g->ref = p[28];
	g->range = g16(p + 29);
}

static int is_head(const uint8_t *b)
{
	return good(b) && memcmp(b, "MUF", 3) == 0 && b[5] == MUF_HEAD && b[4] == 1;
}

/* the head's first block parsed; its continuations through more(i), i from 1 */
static int head(struct muf_reader *r, const uint8_t *b, int (*more)(void *x, unsigned i, uint8_t m[MUF_BLOCK]), void *x)
{
	r->head.letter = (char)b[3];
	r->head.session = g16(b + 6);
	r->head.unit = b[8];
	r->head.chip_id = g64(b + 9);
	memcpy(r->head.fw, b + 17, 3);
	r->head.time_source = b[23];
	r->head.start_us = g64(b + 24);
	r->head.tz_quarters = (int8_t)b[32];
	memcpy(r->head.name, b + 33, 16);
	r->head.name[16] = '\0';
	r->n_streams = b[20];
	r->n_groups = b[21];
	if (r->n_streams > MUF_MAX_STREAMS || r->n_groups > MUF_MAX_GROUPS) {
		return -2;
	}
	for (unsigned s = 0; s < r->n_streams; s++) {
		const uint8_t *q = b + 64 + 8 * s;
		r->streams[s] = (struct muf_stream){ q[0], g32(q + 1), q[5], q[6], q[7] };
	}
	unsigned have = 0;
	for (; have < r->n_groups && have < 8; have++) {
		get_group(&r->groups[have], b + 208 + 36 * have);
	}
	for (unsigned c = 1; c < b[22]; c++) {
		uint8_t m[MUF_BLOCK];
		if (more(x, c, m) != 0 || !good(m) || m[5] != MUF_HEAD_MORE) {
			return -3;
		}
		for (unsigned k = 0; k < 13 && have < r->n_groups; k++) {
			get_group(&r->groups[have++], m + 12 + 36 * k);
		}
	}
	return 0;
}

static int more_read(void *x, unsigned i, uint8_t m[MUF_BLOCK])
{
	struct muf_reader *r = x;

	(void)i;
	return r->read(r->ctx, m);
}

int muf_open(struct muf_reader *r, muf_read_fn read, void *ctx)
{
	uint8_t b[MUF_BLOCK];

	memset(r, 0, sizeof(*r));
	r->read = read;
	r->ctx = ctx;
	if (read(ctx, b) != 0 || !is_head(b)) {
		return -1;
	}
	return head(r, b, more_read, r);
}

struct mem {
	const uint8_t *head;
	unsigned n;
};

static int more_mem(void *x, unsigned i, uint8_t m[MUF_BLOCK])
{
	const struct mem *h = x;

	if (i >= h->n) {
		return -1;
	}
	memcpy(m, h->head + (size_t)MUF_BLOCK * i, MUF_BLOCK);
	return 0;
}

int muf_open_blocks(struct muf_reader *r, const uint8_t *head_blocks, unsigned n, muf_read_fn read, void *ctx)
{
	struct mem h = { head_blocks, n };

	memset(r, 0, sizeof(*r));
	r->read = read;
	r->ctx = ctx;
	if (!n || !is_head(head_blocks)) {
		return -1;
	}
	return head(r, head_blocks, more_mem, &h);
}

unsigned muf_mirror(const uint8_t *tail, unsigned n)
{
	for (unsigned k = 1; k <= n; k++) {
		const uint8_t *b = tail + (size_t)MUF_BLOCK * (n - k);
		if (is_head(b) && b[22] == k) {
			unsigned i = 1;
			while (i < k && good(b + (size_t)MUF_BLOCK * i)) {
				i++;
			}
			if (i == k) {
				return k;
			}
		}
	}
	return 0;
}

static int zeros(const uint8_t *b)
{
	for (unsigned i = 0; i < MUF_BLOCK; i++) {
		if (b[i]) {
			return 0;
		}
	}
	return 1;
}

int muf_next(struct muf_reader *r, struct muf_block *b)
{
	for (;;) {
		int e = r->read(r->ctx, b->raw);
		if (e) {
			return e;
		}
		if (zeros(b->raw) || (good(b->raw) && memcmp(b->raw, "MUF", 3) == 0)) {
			continue;        /* the room unwritten, or the head's mirror */
		}
		if (!good(b->raw)) {
			r->blocks_bad++; /* damaged: skipped, the rest read */
			continue;
		}
		b->kind = b->raw[0];
		b->steps = b->raw[1];
		b->number = g32(b->raw + 4);
		b->first_step = g32(b->raw + 8);
		return 0;
	}
}

int muf_stream_index(const struct muf_reader *r, uint8_t kind)
{
	for (unsigned s = 0; s < r->n_streams; s++) {
		if (r->streams[s].kind == kind) {
			return (int)s;
		}
	}
	return -1;
}

unsigned muf_group_offset(const struct muf_reader *r, const struct muf_group *g)
{
	unsigned off = 0;

	for (unsigned i = 0; i < r->n_groups; i++) {
		const struct muf_group *o = &r->groups[i];
		if (o->stream == g->stream && o->first < g->first) {
			off += o->count * type_size(o->type);
		}
	}
	return off;
}

static double half(uint16_t h)
{
	int e = (h >> 10) & 0x1F, m = h & 0x3FF;
	double v = e == 0 ? ldexp(m, -24) : e == 31 ? (m ? NAN : INFINITY) : ldexp(m + 1024, e - 25);

	return (h & 0x8000) ? -v : v;
}

double muf_raw(const struct muf_reader *r, const struct muf_group *g, const uint8_t *step, unsigned k)
{
	unsigned size = type_size(g->type);
	const uint8_t *p0 = step + muf_group_offset(r, g) + k * size;
	uint8_t p[8];

	for (unsigned i = 0; i < size; i++) {
		p[i] = (g->flags & 1) ? p0[size - 1 - i] : p0[i]; /* bit 0: big-endian */
	}
	switch (g->type) {
	case 'b': return (int8_t)p[0];
	case 'B': return p[0];
	case 'h': return (int16_t)g16(p);
	case 'H': return g16(p);
	case 'i': return (int32_t)g32(p);
	case 'I': return g32(p);
	case 'q': return (double)(int64_t)g64(p);
	case 'Q': return (double)g64(p);
	case 'e': return half(g16(p));
	case 'f': { uint32_t u = g32(p); float f; memcpy(&f, &u, 4); return f; }
	case 'd': { uint64_t u = g64(p); double d; memcpy(&d, &u, 8); return d; }
	default: return NAN;
	}
}

double muf_physical(const struct muf_reader *r, const struct muf_group *g, const uint8_t *step, unsigned k)
{
	double m = g->mantissa ? g->mantissa : 1;

	return (muf_raw(r, g, step, k) + g->add) * m * pow(10.0, g->exp10);
}
