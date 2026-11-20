/* An example of a program's glue for tools/mlfseries.c — the made-up profile X of
 * tests/glue_example.py, its twin, for the tests only — an example, not a profile of the SDK; each program writes its own
 * glue and keeps it in its own repository. */
#include <stdlib.h>
#include <string.h>
#include "../tools/mlfseries.h"
#include "../nrc/nrc.h"

static const uint8_t *body(const mlf_sections *s, uint8_t id, uint32_t *len)
{
	for (size_t i = 0; i < s->n; i++) {
		if (s->s[i].id == id) {
			*len = s->s[i].length;
			return s->s[i].body;
		}
	}
	return NULL;
}

static uint32_t g32(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t g16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

static int chan(const mlf_sections *s, unsigned k, struct mlf_channel *c, unsigned *n)
{
	uint32_t len;
	const uint8_t *b = body(s, 1, &len);

	if (!b || len < 3) {
		return -1;
	}
	*n = b[2];
	if (k >= *n || len < 3 + 23u * (k + 1)) {
		return -1;
	}
	const uint8_t *p = b + 3 + 23 * k;
	memset(c, 0, sizeof(*c));
	memcpy(c->name, p, 8);
	memcpy(c->unit, p + 8, 8);
	c->exp10 = (int8_t)p[16];
	c->mantissa = (int16_t)g16(p + 17);
	c->add = (int32_t)g32(p + 19);
	c->rate_num = g16(b);
	c->rate_den = 1;
	return 0;
}

static int find(const mlf_sections *s, const char *name, struct mlf_channel *c, unsigned *n)
{
	for (unsigned k = 0; chan(s, k, c, n) == 0; k++) {
		if (!strcmp(c->name, name)) {
			return (int)k;
		}
	}
	return -1;
}

static int channel(const mlf_sections *s, const char *name, struct mlf_channel *out)
{
	unsigned n;

	return find(s, name, out, &n) >= 0 ? 0 : -1;
}

static int station(const mlf_sections *s, struct mlf_station *out)
{
	uint32_t len;
	const uint8_t *b = body(s, 2, &len);

	if (!b || len < 42) {
		return -1;
	}
	memset(out, 0, sizeof(*out));
	out->known = 1;
	memcpy(out->name, b, 32);
	out->lat = (int32_t)g32(b + 32) / 1e7;
	out->lon = (int32_t)g32(b + 36) / 1e7;
	out->elev = (int16_t)g16(b + 40);
	return 0;
}

static int samples(const mlf_sections *s, const mlf_object *seg, const char *name, mlf_emit_fn emit, void *ctx)
{
	struct mlf_channel c;
	unsigned n;
	int k = find(s, name, &c, &n);
	uint32_t len;
	const uint8_t *t = body(s, 3, &len);
	int64_t off = t && len ? (int8_t)t[0] : 0, rows = c.rate_num;

	if (k < 0) {
		return -1;
	}
	/* an even run: the time once, in the segment — the first second and the place */
	int64_t base = ((int64_t)seg->first - off) * rows + seg->place;
	if (seg->codec == MLF_RAW) {
		size_t size = (seg->run ? 1 : 6) + 4 * n;
		if (seg->body_len % size) {
			return -1;
		}
		for (size_t i = 0; i < seg->body_len; i += size) {
			const uint8_t *r = seg->body + i;
			uint8_t st = seg->run ? r[0] : r[5];
			int64_t index = seg->run ? base + (int64_t)(i / size) : ((int64_t)g32(r) - off) * rows + r[4];
			emit(ctx, index, (int32_t)g32(r + (seg->run ? 1 : 6) + 4 * k), st == 6 ? 0x02 : st == 7 ? 0x08 : 0);
		}
		return 0;
	}
	if (seg->codec != MLF_NRC) {
		return -1;
	}
	nrc_reader r;
	nrc_reader_init(&r, seg->body, seg->body_len);
	size_t cnt = (size_t)nrc_read_gamma(&r);
	if (r.bad || cnt > seg->body_len * 8) {
		return -1;
	}
	uint32_t *col = calloc((3 + n) * (cnt ? cnt : 1), sizeof(uint32_t));
	int e = 0;
	for (unsigned j = seg->run ? 2 : 0; j < 3 + n && !e; j++) {     /* an even run: no seconds, no frames */
		e = nrc_read_series(&r, cnt, j == 0 || j > 2 ? 32 : 8, j > 2, NULL, col + j * cnt);
	}
	if (!e && nrc_finish(&r) == NRC_OK) {
		for (size_t i = 0; i < cnt; i++) {
			uint8_t st = (uint8_t)col[2 * cnt + i];
			int64_t index = seg->run ? base + (int64_t)i : ((int64_t)col[i] - off) * rows + col[cnt + i];
			emit(ctx, index, (int32_t)col[(3 + k) * cnt + i], st == 6 ? 0x02 : st == 7 ? 0x08 : 0);
		}
	} else {
		e = -1;
	}
	free(col);
	return e ? -1 : 0;
}

const struct mlf_glue mlf_glue = { 'X', channel, station, samples };
