/* NIC-SDK — MUF to miniSEED in C: muf2mseed FILE OUT.mseed NET STA LOC NAME[:k] [LAT LON ELEV]
 * — the twin of tools/muf2mseed.py, the same bytes. One value of a stream, found by its group's
 * name (and its place in the group, k, from 0), as Steim-2 records; a pause in the steps or a
 * missing value ends a run. With the station's place, OUT.mseed.xml beside it: StationXML, the
 * sensitivity from the group's scale. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "muffile.h"
#include "../mseed/mseed.h"

static int to_file(void *ctx, const uint8_t rec[MSEED_RECORD])
{
	return fwrite(rec, MSEED_RECORD, 1, ctx) == 1 ? 0 : -1;
}

static int missing(const struct muf_group *g, double v)
{
	switch (g->type) {
	case 'b': return v == -128.0;
	case 'h': return v == -32768.0;
	case 'i': return v == -2147483648.0;
	case 'B': return v == 255.0;
	case 'H': return v == 65535.0;
	case 'I': return v == 4294967295.0;
	default: return 0;
	}
}

static int32_t *buf;
static size_t len, cap;
static uint64_t run_first;

static int flush(struct mseed_writer *w, uint64_t base)
{
	int e = len ? mseed_run(w, base, run_first, buf, NULL, len) : 0;

	len = 0;
	return e;
}

int main(int argc, char **argv)
{
	static struct muf_reader r;
	static struct muf_block b;
	const struct muf_group *g = NULL;
	struct mseed_writer w;
	char name[16], cha[4];
	unsigned k = 0;

	if (argc != 7 && argc != 10) {
		fprintf(stderr, "muf2mseed FILE OUT.mseed NET STA LOC NAME[:k] [LAT LON ELEV]\n");
		return 2;
	}
	snprintf(name, sizeof(name), "%s", argv[6]);
	char *colon = strchr(name, ':');
	if (colon) {
		*colon = 0;
		k = (unsigned)atoi(colon + 1);
	}
	FILE *f = fopen(argv[1], "rb"), *o;
	if (!f || muf_open_file(&r, f) != 0) {
		fprintf(stderr, "not a MUF file\n");
		return 1;
	}
	for (unsigned s = 0; s < r.n_streams && !g; s++) {      /* in the head's order, as the Python */
		for (unsigned i = 0; i < r.n_groups && !g; i++) {
			if (r.groups[i].stream == r.streams[s].kind && strcmp(r.groups[i].name, name) == 0) {
				g = &r.groups[i];
			}
		}
	}
	if (!g || k >= g->count || !strchr("bBhHiIqQ", g->type)) {
		fprintf(stderr, "no whole-number value %s:%u\n", name, k);
		return 1;
	}
	int s = muf_stream_index(&r, g->stream);
	uint32_t rate = r.streams[s].rate_mhz;
	if (!rate) {
		fprintf(stderr, "an irregular stream has no rate for miniSEED\n");
		return 1;
	}
	char orient = (char)('1' + (k < 9 ? k : 8));
	if (g->count == 1) {
		orient = mseed_orientation(g->name, orient);
	} else if (k < 3) {
		const char axis[2] = { "XYZ"[k], 0 };
		orient = mseed_orientation(axis, orient);
	}
	mseed_channel_code(cha, rate, 1000, g->unit, orient);
	o = fopen(argv[2], "wb");
	if (!o || mseed_begin(&w, argv[3], argv[4], argv[5], cha, rate, 1000, to_file, o) != 0) {
		fprintf(stderr, "cannot write %s\n", argv[2]);
		return 1;
	}
	uint64_t next = 0;
	int e = 0;
	while (!e && muf_next(&r, &b) == 0) {
		if (b.kind != g->stream) {
			continue;
		}
		for (unsigned j = 0; j < b.steps && !e; j++) {
			uint64_t step = (uint64_t)b.first_step + j;
			double v = muf_raw(&r, g, b.raw + MUF_STEPS_FROM + j * r.streams[s].step_bytes, k);
			if (step != next || missing(g, v)) {
				e = flush(&w, r.head.start_us);
			}
			next = step + 1;
			if (missing(g, v)) {
				continue;
			}
			if (v < -2147483648.0 || v > 2147483647.0) {
				fprintf(stderr, "a value past 32 bits at step %llu\n", (unsigned long long)step);
				return 1;
			}
			if (len == cap) {
				cap = cap ? 2 * cap : 4096;
				buf = realloc(buf, cap * sizeof(*buf));
			}
			if (!len) {
				run_first = step;
			}
			buf[len++] = (int32_t)v;
		}
	}
	if (!e) {
		e = flush(&w, r.head.start_us);
	}
	fclose(o);
	if (e) {
		fprintf(stderr, "miniSEED: error %d\n", e);
		return 1;
	}
	if (argc == 10) {
		time_t sec = (time_t)(r.head.start_us / 1000000u);
		struct tm t;
		char start[32], path[1024];
		gmtime_r(&sec, &t);
		strftime(start, sizeof(start), "%Y-%m-%dT%H:%M:%SZ", &t);
		double m = g->mantissa ? g->mantissa : 1;
		const struct mseed_xml_channel ch = { cha, argv[5], g->unit, rate, 1000, 1.0 / (m * pow(10.0, g->exp10)) };
		size_t n = mseed_stationxml(NULL, 0, argv[3], argv[4], r.head.name, start, atof(argv[7]),
					    atof(argv[8]), atof(argv[9]), &ch, 1);
		char *x = malloc(n + 1);
		mseed_stationxml(x, n + 1, argv[3], argv[4], r.head.name, start, atof(argv[7]), atof(argv[8]),
				 atof(argv[9]), &ch, 1);
		snprintf(path, sizeof(path), "%s.xml", argv[2]);
		o = fopen(path, "wb");
		fwrite(x, 1, n, o);
		fclose(o);
		free(x);
	}
	free(buf);
	return 0;
}
