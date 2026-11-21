/* NIC-SDK — MLF to miniSEED in C: mlf2mseed OUT.mseed NET STA LOC NAME FILE… — the twin of
 * tools/mlf2mseed.py, the same bytes. Built with the program's glue for its profile
 * (cc mlf2mseed.c mlfseries.c ../mseed/mseed.c ../mlf/mlf.c your_glue.c): one channel of a series
 * of files, by its name, as Steim-2 records, a gap ending a run, the quality bits as the glue
 * gives them; with the station known from the file, OUT.mseed.xml beside it. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "mlfseries.h"
#include "../mseed/mseed.h"

static int to_file(void *ctx, const uint8_t rec[MSEED_RECORD])
{
	return fwrite(rec, MSEED_RECORD, 1, ctx) == 1 ? 0 : -1;
}

int main(int argc, char **argv)
{
	struct mlf_channel ch;
	struct mlf_station st;
	struct mlf_sample *v;
	size_t n;
	struct mseed_writer w;
	char cha[4];

	if (argc < 7) {
		fprintf(stderr, "mlf2mseed OUT.mseed NET STA LOC NAME FILE...\n");
		return 2;
	}
	if (mlf_series(argv + 6, argc - 6, argv[5], &ch, &st, &v, &n) != 0) {
		return 1;
	}
	mseed_channel_code(cha, ch.rate_num, ch.rate_den, ch.unit, ch.orient ? ch.orient : mseed_orientation(ch.name, '1'));
	FILE *o = fopen(argv[1], "wb");
	if (!o || mseed_begin(&w, argv[2], argv[3], argv[4], cha, ch.rate_num, ch.rate_den, to_file, o) != 0) {
		fprintf(stderr, "cannot write %s\n", argv[1]);
		return 1;
	}
	int32_t *x = malloc((n ? n : 1) * sizeof(*x));
	uint8_t *q = malloc(n ? n : 1);
	int e = 0;
	for (size_t i = 0; i < n && !e;) {      /* a run: numbers one apart */
		size_t j = i;
		for (; j < n && v[j].index == v[i].index + (int64_t)(j - i); j++) {
			x[j - i] = v[j].raw;
			q[j - i] = v[j].quality;
		}
		e = mseed_run(&w, 0, (uint64_t)v[i].index, x, q, j - i);
		i = j;
	}
	fclose(o);
	if (e) {
		fprintf(stderr, "miniSEED: error %d\n", e);
		return 1;
	}
	if (st.known && n) {
		time_t sec = (time_t)(v[0].index * (int64_t)ch.rate_den / (int64_t)ch.rate_num);
		struct tm t;
		char start[32], path[1024];
		gmtime_r(&sec, &t);
		strftime(start, sizeof(start), "%Y-%m-%dT%H:%M:%SZ", &t);
		double m = ch.mantissa ? ch.mantissa : 1;
		const struct mseed_xml_channel xc = { cha, argv[4], ch.unit, ch.rate_num, ch.rate_den,
						      1.0 / (m * pow(10.0, ch.exp10)) };
		size_t len = mseed_stationxml(NULL, 0, argv[2], argv[3], st.name, start, st.lat, st.lon, st.elev, &xc, 1);
		char *xml = malloc(len + 1);
		mseed_stationxml(xml, len + 1, argv[2], argv[3], st.name, start, st.lat, st.lon, st.elev, &xc, 1);
		snprintf(path, sizeof(path), "%s.xml", argv[1]);
		o = fopen(path, "wb");
		fwrite(xml, 1, len, o);
		fclose(o);
		free(xml);
	}
	free(x);
	free(q);
	free(v);
	return 0;
}
