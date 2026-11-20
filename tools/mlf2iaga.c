/* NIC-SDK — MLF to IAGA-2002 in C: mlf2iaga DIR CODE SOURCE minute|second NAME[,NAME…] FILE… — the
 * twin of tools/mlf2iaga.py, the same bytes. Built with the program's glue for its profile, as
 * mlf2mseed. Up to four channels by their names, each component's letter its name's last; the
 * station from the file; the rate whole hertz; a flagged sample left out. One file a UTC day the
 * series touches, named as INTERMAGNET's: DIR/<code><yyyymmdd>v<min|sec>.<min|sec>. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mlfseries.h"
#include "../iaga/iaga.h"

static int to_file(void *ctx, const char *line)
{
	return fputs(line, ctx) < 0 ? -1 : 0;
}

static int64_t floordiv(int64_t a, int64_t b)
{
	return a / b - (a % b != 0 && (a < 0) != (b < 0));
}

int main(int argc, char **argv)
{
	struct mlf_channel ch[4];
	struct mlf_station st;
	struct mlf_sample *v[4];
	size_t nv[4];
	char letters[5] = { 0 }, names[64];
	unsigned n = 0, rate = 0;

	if (argc < 7) {
		fprintf(stderr, "mlf2iaga DIR CODE SOURCE minute|second NAME[,NAME...] FILE...\n");
		return 2;
	}
	int minute = strcmp(argv[4], "second") != 0;
	snprintf(names, sizeof(names), "%s", argv[5]);
	for (char *tok = strtok(names, ","); tok && n < 4; tok = strtok(NULL, ","), n++) {
		if (mlf_series(argv + 6, argc - 6, tok, &ch[n], &st, &v[n], &nv[n]) != 0) {
			return 1;
		}
		if (!ch[n].rate_den || ch[n].rate_num % ch[n].rate_den || (rate && rate != ch[n].rate_num / ch[n].rate_den)) {
			fprintf(stderr, "IAGA-2002 takes one whole number of samples a second\n");
			return 1;
		}
		rate = ch[n].rate_num / ch[n].rate_den;
		size_t len = strlen(ch[n].name);
		char l = len ? ch[n].name[len - 1] : "XYZF"[n];
		letters[n] = (char)(l >= 'a' && l <= 'z' ? l - 32 : l);
	}
	if (!st.known) {
		fprintf(stderr, "no station in the file\n");
		return 1;
	}
	int64_t lo = INT64_MAX, hi = INT64_MIN;
	for (unsigned c = 0; c < n; c++) {
		if (nv[c]) {
			lo = v[c][0].index < lo ? v[c][0].index : lo;
			hi = v[c][nv[c] - 1].index > hi ? v[c][nv[c] - 1].index : hi;
		}
	}
	if (lo > hi) {
		fprintf(stderr, "no samples\n");
		return 1;
	}
	const struct iaga_station sta = { st.name, argv[2], argv[3], st.lat, st.lon, 1, st.elev };
	char code[8];
	unsigned k = 0;
	for (; argv[2][k] && k < 7; k++) {
		code[k] = (char)(argv[2][k] >= 'A' && argv[2][k] <= 'Z' ? argv[2][k] + 32 : argv[2][k]);
	}
	code[k] = 0;
	for (int64_t day = floordiv(lo, (int64_t)rate * 86400); day <= floordiv(hi, (int64_t)rate * 86400); day++) {
		int64_t first;
		size_t count;
		iaga_span(day, rate, minute, &first, &count);
		double *vals[4];
		for (unsigned c = 0; c < n; c++) {
			double m = ch[c].mantissa ? ch[c].mantissa : 1;
			vals[c] = malloc(count * sizeof(double));
			for (size_t i = 0; i < count; i++) {
				vals[c][i] = NAN;
			}
			for (size_t i = 0; i < nv[c]; i++) {
				int64_t at = v[c][i].index - first;
				if (at >= 0 && at < (int64_t)count && !v[c][i].quality) {
					vals[c][at] = ((double)v[c][i].raw + ch[c].add) * m * pow(10.0, ch[c].exp10);
				}
			}
		}
		int64_t z = day + 719468;
		int64_t era = (z >= 0 ? z : z - 146096) / 146097;
		unsigned doe = (unsigned)(z - era * 146097), yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
		unsigned dy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * dy + 2) / 153;
		unsigned dd = dy - (153 * mp + 2) / 5 + 1, mm = mp < 10 ? mp + 3 : mp - 9;
		int yy = (int)(yoe + era * 400 + (mm <= 2));
		char path[1024];
		snprintf(path, sizeof(path), "%s/%s%04d%02u%02uv%s.%s", argv[1], code, yy, mm, dd,
			 minute ? "min" : "sec", minute ? "min" : "sec");
		FILE *o = fopen(path, "w");
		if (!o || iaga_day(to_file, o, &sta, day, rate, minute, n, letters, (const double *const *)vals) != 0) {
			fprintf(stderr, "cannot write %s\n", path);
			return 1;
		}
		fclose(o);
		for (unsigned c = 0; c < n; c++) {
			free(vals[c]);
		}
		printf("%s\n", path);
	}
	for (unsigned c = 0; c < n; c++) {
		free(v[c]);
	}
	return 0;
}
