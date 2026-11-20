/* NIC-SDK — miniSEED 2.4 in plain C: integer samples as Steim-2 in 512 B records, and FDSN
 * StationXML 1.2 beside them. Bound to no container: a caller hands it runs of samples one period
 * apart, read out of whatever holds them (tools/muf2mseed.c reads MUF). Its twin in Python:
 * tools/mseed.py, the same bytes. Floating point only in the StationXML's numbers. */
#ifndef MSEED_H
#define MSEED_H
#include <stddef.h>
#include <stdint.h>

#define MSEED_RECORD 512u
#define MSEED_FRAMES 7u                 /* 64 B each, after the 48 B header and two blockettes */

/* SEED's data-quality bits, ORed over a record's samples */
#define MSEED_CLIPPED 0x02u
#define MSEED_GLITCHES 0x08u

#define MSEED_EOVERFLOW (-1)            /* a difference past Steim-2's 30 bits */
#define MSEED_ERATE (-2)                /* the rate not written in SEED's two 16-bit numbers */
#define MSEED_EWRITE (-3)

typedef int (*mseed_write_fn)(void *ctx, const uint8_t rec[MSEED_RECORD]);

struct mseed_writer {
	char net[3], sta[6], loc[3], cha[4];
	uint64_t num, den;              /* the rate in Hz, num / den, reduced */
	int16_t factor, mult;           /* the same as SEED writes it */
	uint32_t seq;                   /* the next record's sequence number, 1 … 999 999 */
	mseed_write_fn write;
	void *ctx;
};

/* The codes as SEED pads them; the rate as a fraction (a MUF stream's: its mHz over 1000). */
int mseed_begin(struct mseed_writer *w, const char *net, const char *sta, const char *loc,
		const char *cha, uint64_t rate_num, uint64_t rate_den, mseed_write_fn write, void *ctx);

/* A run: n samples one period apart, the k-th at base_us + (first + k) / rate, µs since 1970
 * UTC, rounded to the µs, a half up. quality: each sample's bits, or NULL. A gap is a new run. */
int mseed_run(struct mseed_writer *w, uint64_t base_us, uint64_t first, const int32_t *x,
	      const uint8_t *quality, size_t n);

/* As many samples as the frames take, as Steim-2: words 1 and 2 of frame 0 the first and the last
 * sample, the first difference from prev (0 without one), each word the most differences that
 * fit. Returns the count taken, or MSEED_EOVERFLOW. */
long mseed_steim2(const int32_t *x, size_t n, int32_t prev, int have_prev,
		  uint8_t out[MSEED_FRAMES * 64u]);

/* Back from the frames: count samples into x; 0, or −1 when the integration check fails. */
int mseed_unsteim2(const uint8_t *frames, unsigned nframes, unsigned count, int32_t *x);

/* A channel's code: the band by the rate, the instrument by the unit (m/s2 N, m/s H, nT F, Pa D,
 * K or degC K, else Y), the orientation given. */
void mseed_channel_code(char out[4], uint64_t rate_num, uint64_t rate_den, const char *unit, char orient);

/* The orientation by a name's last letter: Z N E as they are, X 1, Y 2; otherwise fallback. */
char mseed_orientation(const char *name, char fallback);

/* One StationXML document for one station; a channel each entry. Writes at most cap bytes with
 * the NUL and returns the length the whole would take. */
struct mseed_xml_channel {
	const char *cha, *loc, *unit;
	uint64_t rate_num, rate_den;
	double counts_per_unit;         /* 1 / (mantissa · 10^exp10) */
};

size_t mseed_stationxml(char *buf, size_t cap, const char *net, const char *sta, const char *site,
			const char *start, double lat, double lon, double elev,
			const struct mseed_xml_channel *ch, unsigned n);

#endif
