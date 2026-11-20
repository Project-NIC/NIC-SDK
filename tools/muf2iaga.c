/* NIC-SDK — MUF to IAGA-2002 in C: muf2iaga FILE DIR CODE LAT LON ELEV|- SOURCE [minute|second]
 * — the twin of tools/muf2iaga.py, the same bytes. The magnetometer found by its description:
 * the values in nT of the first stream that has them, up to four — a group's letters its name's
 * when they are as many as its values, else X Y Z F; each sample's time from the head's start and
 * the stream's rate, which must be whole hertz. One file a UTC day the record touches, named as
 * INTERMAGNET's: DIR/<code><yyyymmdd>v<min|sec>.<min|sec>. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "muffile.h"
#include "../iaga/iaga.h"

static int to_file(void *ctx, const char *line)
{
	return fputs(line, ctx) < 0 ? -1 : 0;
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
	default: return v != v;
	}
}

static int64_t floordiv(int64_t a, int64_t b)
{
	return a / b - (a % b != 0 && (a < 0) != (b < 0));
}

int main(int argc, char **argv)
{
	static struct muf_reader r;
	static struct muf_block b;
	const struct muf_group *g[4];
	unsigned gk[4], n = 0;
	char letters[5] = { 0 };
	int stream = -1;

	if (argc != 8 && argc != 9) {
		fprintf(stderr, "muf2iaga FILE DIR CODE LAT LON ELEV|- SOURCE [minute|second]\n");
		return 2;
	}
	int minute = argc == 8 || strcmp(argv[8], "second") != 0;
	FILE *f = fopen(argv[1], "rb");
	if (!f || muf_open_file(&r, f) != 0) {
		fprintf(stderr, "not a MUF file\n");
		return 1;
	}
	for (unsigned s = 0; s < r.n_streams; s++) {            /* in the head's order, as the Python */
		for (unsigned i = 0; i < r.n_groups; i++) {
			const struct muf_group *c = &r.groups[i];
			if (c->stream != r.streams[s].kind || strcmp(c->unit, "nT") != 0 ||
			    (stream >= 0 && c->stream != stream)) {
				continue;
			}
			stream = c->stream;
			size_t len = strlen(c->name);
			for (unsigned k = 0; k < c->count && n < 4; k++, n++) {
				char l = len == c->count ? c->name[k] : c->count == 1 && len ? c->name[len - 1] : "XYZF"[n];
				letters[n] = (char)(l >= 'a' && l <= 'z' ? l - 32 : l);
				g[n] = c, gk[n] = k;
			}
		}
	}
	if (!n) {
		fprintf(stderr, "no values in nT\n");
		return 1;
	}
	int si = muf_stream_index(&r, (uint8_t)stream);
	uint32_t mhz = r.streams[si].rate_mhz;
	if (!mhz || mhz % 1000) {
		fprintf(stderr, "IAGA-2002 takes a whole number of samples a second\n");
		return 1;
	}
	unsigned rate = mhz / 1000;
	uint64_t at = r.head.start_us * rate;
	int64_t base = (int64_t)((at + 500000u) / 1000000u);
	if (at % 1000000u) {
		fprintf(stderr, "the start is not on a sample: moved to the nearest, %lld µs\n",
			(long long)((int64_t)(base * 1000000) - (int64_t)at) / (long long)rate);
	}
	int64_t *num = NULL;
	double *val = NULL;
	size_t len = 0, cap = 0;
	while (muf_next(&r, &b) == 0) {
		if (b.kind != stream) {
			continue;
		}
		for (unsigned j = 0; j < b.steps; j++) {
			const uint8_t *st = b.raw + MUF_STEPS_FROM + j * r.streams[si].step_bytes;
			if (len == cap) {
				cap = cap ? 2 * cap : 65536;
				num = realloc(num, cap * sizeof(*num));
				val = realloc(val, cap * 4 * sizeof(*val));
			}
			num[len] = base + (int64_t)b.first_step + j;
			for (unsigned c = 0; c < n; c++) {
				double v = muf_raw(&r, g[c], st, gk[c]);
				val[4 * len + c] = missing(g[c], v) ? NAN : muf_physical(&r, g[c], st, gk[c]);
			}
			len++;
		}
	}
	if (!len) {
		fprintf(stderr, "no samples\n");
		return 1;
	}
	const struct iaga_station sta = { r.head.name, argv[3], argv[7], atof(argv[4]), atof(argv[5]),
					  strcmp(argv[6], "-") != 0, atof(argv[6]) };
	int64_t d0 = floordiv(num[0], (int64_t)rate * 86400), d1 = floordiv(num[len - 1], (int64_t)rate * 86400);
	for (int64_t day = d0; day <= d1; day++) {
		int64_t first;
		size_t count;
		iaga_span(day, rate, minute, &first, &count);
		double *v[4];
		for (unsigned c = 0; c < n; c++) {
			v[c] = malloc(count * sizeof(double));
			for (size_t i = 0; i < count; i++) {
				v[c][i] = NAN;
			}
		}
		for (size_t i = 0; i < len; i++) {
			if (num[i] >= first && num[i] < first + (int64_t)count) {
				for (unsigned c = 0; c < n; c++) {
					v[c][num[i] - first] = val[4 * i + c];
				}
			}
		}
		/* the file's name: the code in small letters, the date, v for variation, the interval */
		int64_t z = day;
		char path[1024], code[8];
		unsigned k = 0;
		for (; argv[3][k] && k < 7; k++) {
			code[k] = (char)(argv[3][k] >= 'A' && argv[3][k] <= 'Z' ? argv[3][k] + 32 : argv[3][k]);
		}
		code[k] = 0;
		z += 719468;
		int64_t era = (z >= 0 ? z : z - 146096) / 146097;
		unsigned doe = (unsigned)(z - era * 146097), yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
		unsigned dy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * dy + 2) / 153;
		unsigned dd = dy - (153 * mp + 2) / 5 + 1, mm = mp < 10 ? mp + 3 : mp - 9;
		int yy = (int)(yoe + era * 400 + (mm <= 2));
		snprintf(path, sizeof(path), "%s/%s%04d%02u%02uv%s.%s", argv[2], code, yy, mm, dd,
			 minute ? "min" : "sec", minute ? "min" : "sec");
		FILE *o = fopen(path, "w");
		if (!o || iaga_day(to_file, o, &sta, day, rate, minute, n, letters, (const double *const *)v) != 0) {
			fprintf(stderr, "cannot write %s\n", path);
			return 1;
		}
		fclose(o);
		for (unsigned c = 0; c < n; c++) {
			free(v[c]);
		}
		printf("%s\n", path);
	}
	free(num);
	free(val);
	return 0;
}
