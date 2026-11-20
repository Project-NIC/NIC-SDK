/* NIC-SDK — IAGA-2002 in plain C: a magnetometer's day as INTERMAGNET's exchange text, minute
 * values by INTERMAGNET's Gaussian or second values, faster samples first brought to 1 Hz by a
 * low-pass. Bound to no container: a caller hands it each component's samples in nT on the day's
 * span (tools/muf2iaga.c reads MUF). Its twin in Python: tools/iaga.py, the same bytes — the
 * floating point in one order in both, compiled without contraction (-ffp-contract=off; C11's
 * strict mode has it so). */
#ifndef IAGA_H
#define IAGA_H
#include <stddef.h>
#include <stdint.h>

#define IAGA_GAUSS_TAPS 91u
#define IAGA_GAUSS_SIGMA 15.8734        /* s — INTERMAGNET's minute filter, as USGS's geomag-algorithms */

typedef int (*iaga_write_fn)(void *ctx, const char *line);   /* one line with its newline; 0 good */

struct iaga_station {
	const char *name, *code, *source;   /* the code three letters, as IAGA gives it */
	double lat, lon;                    /* degrees, geodetic */
	int has_elev;
	double elev;                        /* m */
};

/* The low-pass from rate Hz to 1 Hz: a windowed sinc at 0.25 Hz, Blackman, 2·⌊123·rate/20⌋ + 1
 * taps (USGS's 10 Hz filter's 123 scaled to the rate), summing to 1. Returns the taps; w may be
 * NULL to ask. */
unsigned iaga_lowpass(unsigned rate, double *w);
void iaga_gaussian(double w[IAGA_GAUSS_TAPS]);

/* The samples a day needs, by their number t·rate since 1970: from *first, *count of them. */
void iaga_span(int64_t day, unsigned rate, int minute, int64_t *first, size_t *count);

/* One station's day (days since 1970, UTC): up to four components, each count samples from the
 * span's first, NaN where none; letters the components' (X Y Z F …). A value that cannot be
 * filled — fewer than nine tenths of a filter's inputs — 99999.00; a column with no component
 * 88888.00. 0, or what write returned. */
int iaga_day(iaga_write_fn write, void *ctx, const struct iaga_station *st, int64_t day,
	     unsigned rate, int minute, unsigned ncomp, const char *letters, const double *const vals[]);

#endif
