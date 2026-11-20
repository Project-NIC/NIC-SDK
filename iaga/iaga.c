/* NIC-SDK — IAGA-2002 in plain C; see iaga.h. Its twin: tools/iaga.py. */
#include "iaga.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PI 3.14159265358979323846

void iaga_gaussian(double w[IAGA_GAUSS_TAPS])
{
	double s = 0;

	for (unsigned n = 0; n < IAGA_GAUSS_TAPS; n++) {
		double d = ((double)n - 45.0) / IAGA_GAUSS_SIGMA;
		w[n] = exp(-0.5 * (d * d));
		s += w[n];
	}
	for (unsigned n = 0; n < IAGA_GAUSS_TAPS; n++) {
		w[n] /= s;
	}
}

unsigned iaga_lowpass(unsigned rate, double *w)
{
	unsigned m = 123u * rate / 20u, taps = 2u * m + 1u;
	double fc = 2 * 0.25 / rate, s = 0;

	if (!w) {
		return taps;
	}
	for (unsigned n = 0; n < taps; n++) {
		double x = (double)n - (double)m;
		double sinc = x == 0 ? 1.0 : sin(PI * fc * x) / (PI * fc * x);
		double win = 0.42 - 0.5 * cos(2 * PI * n / (taps - 1)) + 0.08 * cos(4 * PI * n / (taps - 1));
		w[n] = fc * sinc * win;
		s += w[n];
	}
	for (unsigned n = 0; n < taps; n++) {
		w[n] /= s;
	}
	return taps;
}

/* the seconds a day's values are made from */
static void seconds(int64_t day, int minute, int64_t *s0, int64_t *s1)
{
	*s0 = day * 86400 - (minute ? 45 : 0);
	*s1 = (day + 1) * 86400 - (minute ? 15 : 1);
}

void iaga_span(int64_t day, unsigned rate, int minute, int64_t *first, size_t *count)
{
	int64_t s0, s1, m = rate > 1 ? (int64_t)(123u * rate / 20u) : 0;

	seconds(day, minute, &s0, &s1);
	*first = s0 * rate - m;
	*count = (size_t)((s1 - s0) * rate + 1 + 2 * m);
}

/* one output from the inputs centre − m … centre + m: the weights over the values present,
 * renormalised; NaN with fewer than nine tenths of them */
static double filt(const double *w, unsigned taps, const double *in, int64_t in_first, size_t n, int64_t centre)
{
	int64_t m = (taps - 1) / 2;
	double acc = 0, wsum = 0;
	unsigned have = 0;

	for (unsigned j = 0; j < taps; j++) {
		int64_t i = centre + j - m - in_first;
		if (i >= 0 && (size_t)i < n && !isnan(in[i])) {
			acc += w[j] * in[i];
			wsum += w[j];
			have++;
		}
	}
	return 10u * have >= 9u * taps && wsum != 0 ? acc / wsum : NAN;
}

static void civil(int64_t z, int *y, unsigned *m, unsigned *d, unsigned *doy)
{
	z += 719468;
	int64_t era = (z >= 0 ? z : z - 146096) / 146097;
	unsigned doe = (unsigned)(z - era * 146097);
	unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	unsigned dy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	unsigned mp = (5 * dy + 2) / 153;
	*d = dy - (153 * mp + 2) / 5 + 1;
	*m = mp < 10 ? mp + 3 : mp - 9;
	*y = (int)(yoe + era * 400 + (*m <= 2));
	int leap = (*y % 4 == 0 && *y % 100 != 0) || *y % 400 == 0;
	static const unsigned before[] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };
	*doy = before[*m - 1] + *d + (leap && *m > 2);
}

static int head(iaga_write_fn write, void *ctx, const char *label, const char *value)
{
	char line[96];

	snprintf(line, sizeof(line), " %-23s%-45.45s|\n", label, value);
	return write(ctx, line);
}

int iaga_day(iaga_write_fn write, void *ctx, const struct iaga_station *st, int64_t day,
	     unsigned rate, int minute, unsigned ncomp, const char *letters, const double *const vals[])
{
	int64_t s0, s1, first;
	size_t count;
	double g[IAGA_GAUSS_TAPS], *lp = NULL, *col[4] = { 0 };
	unsigned taps = rate > 1 ? iaga_lowpass(rate, NULL) : 1;
	char names[5] = { 0 }, given[5] = { 0 }, t[96];
	int e = 0;

	if (ncomp > 4) {
		ncomp = 4;
	}
	seconds(day, minute, &s0, &s1);
	iaga_span(day, rate, minute, &first, &count);
	iaga_gaussian(g);
	if (rate > 1) {
		lp = malloc(taps * sizeof(double));
		iaga_lowpass(rate, lp);
	}
	size_t ns = (size_t)(s1 - s0 + 1), nout = minute ? 1440 : 86400;
	double *sec = malloc(ns * sizeof(double));
	for (unsigned c = 0; c < ncomp; c++) {
		for (size_t i = 0; i < ns; i++) {
			sec[i] = rate == 1 ? vals[c][i] : filt(lp, taps, vals[c], first, count, (s0 + (int64_t)i) * rate);
		}
		col[c] = malloc(nout * sizeof(double));
		for (size_t i = 0; i < nout; i++) {
			col[c][i] = minute ? filt(g, IAGA_GAUSS_TAPS, sec, s0, ns, day * 86400 + (int64_t)i * 60)
					   : sec[i];
		}
		given[c] = names[c] = letters[c];
	}
	for (unsigned c = ncomp; c < 4; c++) {
		names[c] = strchr(names, 'F') ? 'G' : 'F';
	}
	e = e ? e : head(write, ctx, "Format", "IAGA-2002");
	e = e ? e : head(write, ctx, "Source of Data", st->source);
	e = e ? e : head(write, ctx, "Station Name", st->name);
	e = e ? e : head(write, ctx, "IAGA Code", st->code);
	snprintf(t, sizeof(t), "%.3f", st->lat);
	e = e ? e : head(write, ctx, "Geodetic Latitude", t);
	double lon = fmod(st->lon, 360.0);
	snprintf(t, sizeof(t), "%07.3f", lon < 0 ? lon + 360.0 : lon);
	e = e ? e : head(write, ctx, "Geodetic Longitude", t);
	if (st->has_elev) {
		snprintf(t, sizeof(t), "%g", st->elev);
	} else {
		t[0] = 0;
	}
	e = e ? e : head(write, ctx, "Elevation", t);
	e = e ? e : head(write, ctx, "Reported", names);
	e = e ? e : head(write, ctx, "Sensor Orientation", given);
	if (rate == 1) {
		snprintf(t, sizeof(t), "1 second");
	} else {
		snprintf(t, sizeof(t), "%g second", 1.0 / rate);
	}
	e = e ? e : head(write, ctx, "Digital Sampling", t);
	e = e ? e : head(write, ctx, "Data Interval Type", minute ? "Filtered 1-minute (00:15-01:45)" :
				rate == 1 ? "1-second" : "Filtered 1-second");
	e = e ? e : head(write, ctx, "Data Type", "variation");
	if (minute) {
		e = e ? e : head(write, ctx, "# Minute values", "Gaussian, 91 taps, sigma 15.8734 s, centred");
	}
	if (rate != 1) {
		snprintf(t, sizeof(t), "low-pass 0.25 Hz, %u taps, Blackman", taps);
		e = e ? e : head(write, ctx, "# Second values", t);
	}
	char c4[4][16];
	for (unsigned c = 0; c < 4; c++) {
		snprintf(c4[c], sizeof(c4[c]), "%s%c", st->code, names[c]);
	}
	snprintf(t, sizeof(t), "DATE       TIME         DOY     %-10s%-10s%-10s%-7s|\n", c4[0], c4[1], c4[2], c4[3]);
	e = e ? e : write(ctx, t);
	for (size_t i = 0; i < nout && !e; i++) {
		int64_t k = day * 86400 + (int64_t)i * (minute ? 60 : 1);
		int y;
		unsigned mo, d, doy, s = (unsigned)(k % 86400);
		double v[4];
		civil(day, &y, &mo, &d, &doy);
		for (unsigned c = 0; c < 4; c++) {
			v[c] = c >= ncomp ? 88888.0 : isnan(col[c][i]) ? 99999.0 : col[c][i];
		}
		snprintf(t, sizeof(t), "%04d-%02u-%02u %02u:%02u:%02u.000 %03u   %10.2f%10.2f%10.2f%10.2f\n", y, mo, d,
			 s / 3600, s / 60 % 60, s % 60, doy, v[0], v[1], v[2], v[3]);
		e = write(ctx, t);
	}
	for (unsigned c = 0; c < 4; c++) {
		free(col[c]);
	}
	free(sec);
	free(lp);
	return e;
}
