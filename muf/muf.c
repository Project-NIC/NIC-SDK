/* NIC-SDK — MUF's writer. */
#define _POSIX_C_SOURCE 200809L /* strnlen */
#include <string.h>
#include "muf.h"

static void p16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void p32(uint8_t *p, uint32_t v) { p16(p, (uint16_t)v); p16(p + 2, (uint16_t)(v >> 16)); }
static void p64(uint8_t *p, uint64_t v) { p32(p, (uint32_t)v); p32(p + 4, (uint32_t)(v >> 32)); }

/* a fixed field, NUL-padded, cut at its length — a name filling it has no NUL, as MUF.md allows */
static void put_text(uint8_t *p, const char *t, size_t n)
{
	size_t k = strnlen(t, n);

	memcpy(p, t, k);
}

static uint32_t crc32(const uint8_t *d, size_t n)
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

static int emit(struct muf_writer *w, uint8_t *b)
{
	p32(b + 508, crc32(b, 508));
	if (!w->error && w->room && w->pos >= w->room - w->heads) {
		w->error = -2;                  /* the room full: the mirror reached */
	}
	if (!w->error && w->write(w->ctx, b) != 0) {
		w->error = -1;
	}
	if (!w->error) {
		w->pos++;
	}
	return w->error;
}

static int is_indexed(const struct muf_writer *w, unsigned s)
{
	return w->streams[s].kind != MUF_CONST;
}

static void range_start(struct muf_writer *w)
{
	unsigned slot = 0;

	w->range_first = w->number;
	w->range_n = 0;
	memset(w->range_next, 0, sizeof(w->range_next));
	for (unsigned s = 0; s < w->n_streams && slot < MUF_INDEX_SLOTS; s++) {
		if (is_indexed(w, s)) {
			w->range_next[slot++] = w->next_step[s] - w->filled[s];
		}
	}
}

static int write_index(struct muf_writer *w)
{
	uint8_t b[MUF_BLOCK] = { 0 };

	b[0] = MUF_INDEX;
	p16(b + 2, w->head.session);
	p32(b + 4, w->number);
	p32(b + 12, w->range_first);
	for (unsigned k = 0; k < MUF_INDEX_SLOTS; k++) {
		p32(b + 28 + 4 * k, w->range_next[k]);
	}
	for (unsigned k = 0; k < w->range_n; k++) {
		b[96 + 2 * k] = w->range_kind[k];
		b[97 + 2 * k] = w->range_steps[k];
	}
	w->number++;
	emit(w, b);
	range_start(w);
	return w->error;
}

/* A numbered block: give it its number, note it in the range, index after every 200. */
static int numbered(struct muf_writer *w, uint8_t *b)
{
	p32(b + 4, w->number);
	w->range_kind[w->range_n] = b[0];
	w->range_steps[w->range_n] = b[1];
	w->range_n++;
	w->number++;
	emit(w, b);
	if (w->range_n == MUF_INDEX_EVERY) {
		write_index(w);
	}
	return w->error;
}

static void put_group(uint8_t *p, const struct muf_group *g)
{
	p[0] = g->stream;
	p[1] = g->first;
	p[2] = g->count;
	p[3] = (uint8_t)g->type;
	p[4] = g->flags;
	p[5] = (uint8_t)g->exp10;
	p16(p + 6, (uint16_t)g->mantissa);
	p32(p + 8, (uint32_t)g->add);
	put_text(p + 12, g->name, 8);
	put_text(p + 20, g->unit, 8);
	p[28] = g->ref;
	p16(p + 29, g->range);
}

int muf_begin(struct muf_writer *w, muf_write_fn write, void *ctx, const struct muf_head *h,
	      const struct muf_stream *streams, unsigned n_streams, const struct muf_group *groups,
	      unsigned n_groups, const char *table_csv, const uint8_t *constants_step, muf_place_fn place,
	      uint32_t room)
{
	uint8_t b[MUF_BLOCK];
	unsigned more = n_groups > 8 ? (n_groups - 8 + 12) / 13 : 0;

	if (n_streams > MUF_MAX_STREAMS || n_groups > 8 + 13 * 255) {
		return -1;
	}
	memset(w, 0, sizeof(*w));
	w->write = write;
	w->place = place;
	w->ctx = ctx;
	w->heads = 1 + more;
	w->room = place ? room : 0;
	if (w->room && w->room < 2 * w->heads + 1) {
		return -1;
	}
	w->head = *h;
	w->n_streams = n_streams;
	memcpy(w->streams, streams, n_streams * sizeof(*streams));

	/* the head */
	memset(b, 0, sizeof(b));
	memcpy(b, "MUF", 3);
	b[3] = (uint8_t)h->letter;
	b[4] = 1;
	b[5] = MUF_HEAD;
	p16(b + 6, h->session);
	b[8] = h->unit;
	p64(b + 9, h->chip_id);
	memcpy(b + 17, h->fw, 3);
	b[20] = (uint8_t)n_streams;
	b[21] = (uint8_t)n_groups;
	b[22] = (uint8_t)(1 + more);
	b[23] = h->time_source;
	p64(b + 24, h->start_us);
	b[32] = (uint8_t)h->tz_quarters;
	put_text(b + 33, h->name, 16);
	for (unsigned s = 0; s < n_streams; s++) {
		uint8_t *q = b + 64 + 8 * s;
		q[0] = streams[s].kind;
		p32(q + 1, streams[s].rate_mhz);
		q[5] = streams[s].steps_per_block;
		q[6] = streams[s].step_bytes;
		q[7] = streams[s].flags;
	}
	for (unsigned g = 0; g < n_groups && g < 8; g++) {
		put_group(b + 208 + 36 * g, &groups[g]);
	}
	if (emit(w, b) || (w->room && w->place(w->ctx, w->room - w->heads, b) != 0)) {
		return w->error ? w->error : (w->error = -1);
	}
	/* its continuations, thirteen groups each */
	for (unsigned c = 0; c < more; c++) {
		memset(b, 0, sizeof(b));
		memcpy(b, "MUF", 3);
		b[3] = (uint8_t)h->letter;
		b[4] = 1;
		b[5] = MUF_HEAD_MORE;
		p16(b + 6, h->session);
		for (unsigned k = 0; k < 13 && 8 + 13 * c + k < n_groups; k++) {
			put_group(b + 12 + 36 * k, &groups[8 + 13 * c + k]);
		}
		if (emit(w, b) || (w->room && w->place(w->ctx, w->room - w->heads + 1 + c, b) != 0)) {
			return w->error ? w->error : (w->error = -1);
		}
	}
	range_start(w);
	/* the set's table, as many blocks as it needs */
	size_t len = table_csv ? strlen(table_csv) : 0, off = 0;
	while (off < len) {
		size_t part = len - off > 494 ? 494 : len - off;
		memset(b, 0, sizeof(b));
		b[0] = MUF_TABLE;
		p16(b + 2, h->session);
		p16(b + 12, (uint16_t)len);
		memcpy(b + 14, table_csv + off, part);
		off += part;
		numbered(w, b);
	}
	/* the constants, one step, no time */
	for (unsigned s = 0; s < n_streams; s++) {
		if (streams[s].kind == MUF_CONST && constants_step) {
			memset(b, 0, sizeof(b));
			b[0] = MUF_CONST;
			b[1] = 1;
			p16(b + 2, h->session);
			memcpy(b + MUF_STEPS_FROM, constants_step, streams[s].step_bytes);
			numbered(w, b);
		}
	}
	return w->error;
}

int muf_flush(struct muf_writer *w, unsigned s)
{
	uint8_t *b = w->buf[s];

	if (w->filled[s] == 0) {
		return w->error;
	}
	b[0] = w->streams[s].kind;
	b[1] = w->filled[s];
	p16(b + 2, w->head.session);
	p32(b + 8, w->next_step[s] - w->filled[s]);
	w->filled[s] = 0;     /* before numbered(): an index written there must see this block done */
	numbered(w, b);
	memset(b, 0, MUF_BLOCK);
	return w->error;
}

int muf_step(struct muf_writer *w, unsigned s, const uint8_t *step)
{
	const struct muf_stream *st = &w->streams[s];

	if (s >= w->n_streams) {
		return -1;
	}
	memcpy(w->buf[s] + MUF_STEPS_FROM + (size_t)w->filled[s] * st->step_bytes, step, st->step_bytes);
	w->filled[s]++;
	w->next_step[s]++;
	if (w->filled[s] == st->steps_per_block) {
		return muf_flush(w, s);
	}
	return w->error;
}

void muf_skip(struct muf_writer *w, unsigned s, uint32_t steps)
{
	muf_flush(w, s);
	w->next_step[s] += steps;
}

int muf_end(struct muf_writer *w, uint8_t why)
{
	uint8_t b[MUF_BLOCK] = { 0 };
	uint32_t fast = 0;

	for (unsigned s = 0; s < w->n_streams; s++) {
		muf_flush(w, s);
		if (w->streams[s].kind == MUF_DATA0) {
			fast = w->next_step[s];
		}
	}
	if (w->range_n) {
		write_index(w);   /* the last range's index, just before the end */
	}
	b[0] = MUF_END;
	p16(b + 2, w->head.session);
	p32(b + 4, w->number);
	b[12] = why;
	p32(b + 16, fast);
	p32(b + 20, w->number + 1);
	p32(b + 24, w->dropped);
	w->number++;
	return emit(w, b);
}
