/* NIC-SDK — MUF to GPX in C: muf2gpx FILE OUT.gpx — the twin of tools/muf2gpx.py, the same bytes.
 * The position found by its description: two values in degrees, and a height of one in metres. */
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "../muf/muf_read.h"

static int from_file(void *ctx, uint8_t b[MUF_BLOCK])
{
	size_t n = fread(b, 1, MUF_BLOCK, ctx);

	return n == MUF_BLOCK ? 0 : n == 0 ? 1 : -1;
}

static int64_t raw_int(const struct muf_reader *r, const struct muf_group *g, const uint8_t *s, unsigned k)
{
	return (int64_t)muf_raw(r, g, s, k);
}

static int missing(const struct muf_group *g, int64_t v)
{
	switch (g->type) {
	case 'b': return v == -128;
	case 'h': return v == -32768;
	case 'i': return v == INT32_MIN;
	default: return 0;
	}
}

/* an integer × 10^exp10 written exactly */
static void fixed(FILE *o, int64_t raw, int exp10)
{
	int neg = raw < 0;
	uint64_t a = neg ? (uint64_t)(-raw) : (uint64_t)raw, p = 1;

	if (exp10 >= 0) {
		for (int k = 0; k < exp10; k++) {
			a *= 10;
		}
		fprintf(o, "%s%" PRIu64, neg ? "-" : "", a);
		return;
	}
	for (int k = 0; k < -exp10; k++) {
		p *= 10;
	}
	fprintf(o, "%s%" PRIu64 ".%0*" PRIu64, neg ? "-" : "", a / p, -exp10, a % p);
}

int main(int argc, char **argv)
{
	static struct muf_reader r;
	static struct muf_block b;
	const struct muf_group *ll = NULL, *h = NULL;

	if (argc < 3) {
		fprintf(stderr, "muf2gpx FILE OUT.gpx\n");
		return 2;
	}
	FILE *f = fopen(argv[1], "rb"), *o;
	if (!f || muf_open(&r, from_file, f) != 0) {
		fprintf(stderr, "not a MUF file\n");
		return 1;
	}
	for (unsigned s = 0; s < r.n_streams && !ll; s++) {     /* in the head's order, as the Python */
		for (unsigned g = 0; g < r.n_groups; g++) {
			if (r.groups[g].stream == r.streams[s].kind && r.groups[g].count == 2 &&
			    strcmp(r.groups[g].unit, "deg") == 0 && (!ll || r.groups[g].first < ll->first)) {
				ll = &r.groups[g];
			}
		}
	}
	if (!ll) {
		fprintf(stderr, "no position (a group of two values in degrees)\n");
		return 1;
	}
	for (unsigned g = 0; g < r.n_groups; g++) {
		const struct muf_group *c = &r.groups[g];
		if (c->stream == ll->stream && c->count == 1 && strcmp(c->unit, "m") == 0 && (!h || c->first < h->first)) {
			h = c;
		}
	}
	int s = muf_stream_index(&r, ll->stream);
	uint32_t rate = r.streams[s].rate_mhz;
	int64_t m = ll->mantissa ? ll->mantissa : 1;
	o = fopen(argv[2], "w");
	fprintf(o, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
		   "<gpx version=\"1.1\" creator=\"NIC-SDK muf2gpx\" xmlns=\"http://www.topografix.com/GPX/1/1\">\n"
		   "<trk><name>%s</name><trkseg>\n", r.head.name);
	while (muf_next(&r, &b) == 0) {
		if (b.kind != ll->stream) {
			continue;
		}
		for (unsigned k = 0; k < b.steps; k++) {
			const uint8_t *st = b.raw + MUF_STEPS_FROM + k * r.streams[s].step_bytes;
			int64_t lat = raw_int(&r, ll, st, 0), lon = raw_int(&r, ll, st, 1);
			if (missing(ll, lat) || missing(ll, lon)) {
				continue;
			}
			fprintf(o, "<trkpt lat=\"");
			fixed(o, (lat + ll->add) * m, ll->exp10);
			fprintf(o, "\" lon=\"");
			fixed(o, (lon + ll->add) * m, ll->exp10);
			fprintf(o, "\">");
			if (h) {
				int64_t v = raw_int(&r, h, st, 0);
				if (!missing(h, v)) {
					fprintf(o, "<ele>");
					fixed(o, (v + h->add) * (h->mantissa ? h->mantissa : 1), h->exp10);
					fprintf(o, "</ele>");
				}
			}
			if (r.head.start_us && rate) {
				uint64_t us = r.head.start_us + (uint64_t)(b.first_step + k) * 1000000000ull / rate;
				time_t sec = (time_t)(us / 1000000u);
				struct tm t;
				char iso[32];
				gmtime_r(&sec, &t);
				strftime(iso, sizeof(iso), "%Y-%m-%dT%H:%M:%S", &t);
				fprintf(o, "<time>%s.%03uZ</time>", iso, (unsigned)(us % 1000000u / 1000u));
			}
			fprintf(o, "</trkpt>\n");
		}
	}
	fprintf(o, "</trkseg></trk>\n</gpx>\n");
	fclose(o);
	return 0;
}
