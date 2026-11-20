/* NIC-SDK — miniSEED 2.4 in plain C; see mseed.h. Its twin: tools/mseed.py. */
#include "mseed.h"
#include <stdio.h>
#include <string.h>

static const struct { unsigned count, bits, nib; int dnib; } forms[] = {
	{ 7, 4, 3, 2 }, { 6, 5, 3, 1 }, { 5, 6, 3, 0 }, { 4, 8, 1, -1 },
	{ 3, 10, 2, 3 }, { 2, 15, 2, 2 }, { 1, 30, 2, 1 },
};

static void put16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void put32(uint8_t *p, uint32_t v) { put16(p, v >> 16); put16(p + 2, v); }
static uint32_t get32(const uint8_t *p)
{
	return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static int fits(int64_t d, unsigned bits)
{
	return d >= -((int64_t)1 << (bits - 1)) && d < ((int64_t)1 << (bits - 1));
}

static uint64_t gcd(uint64_t a, uint64_t b)
{
	while (b) {
		uint64_t t = a % b;
		a = b;
		b = t;
	}
	return a;
}

static void pad(char *dst, const char *src, unsigned n)
{
	unsigned i = 0;

	for (; src && src[i] && i < n; i++) {
		dst[i] = src[i];
	}
	for (; i < n; i++) {
		dst[i] = ' ';
	}
	dst[n] = 0;
}

int mseed_begin(struct mseed_writer *w, const char *net, const char *sta, const char *loc,
		const char *cha, uint64_t rate_num, uint64_t rate_den, mseed_write_fn write, void *ctx)
{
	uint64_t g = gcd(rate_num, rate_den);

	memset(w, 0, sizeof(*w));
	pad(w->net, net, 2);
	pad(w->sta, sta, 5);
	pad(w->loc, loc, 2);
	pad(w->cha, cha, 3);
	if (!rate_num || !rate_den) {
		return MSEED_ERATE;
	}
	w->num = rate_num / g;
	w->den = rate_den / g;
	if (w->den == 1 && w->num <= 32767) {
		w->factor = (int16_t)w->num, w->mult = 1;
	} else if (w->num == 1 && w->den <= 32767) {
		w->factor = (int16_t)-(int64_t)w->den, w->mult = 1;
	} else if (w->num <= 32767 && w->den <= 32767) {
		w->factor = (int16_t)w->num, w->mult = (int16_t)-(int64_t)w->den;
	} else {
		return MSEED_ERATE;
	}
	w->seq = 1;
	w->write = write;
	w->ctx = ctx;
	return 0;
}

long mseed_steim2(const int32_t *x, size_t n, int32_t prev, int have_prev, uint8_t out[MSEED_FRAMES * 64u])
{
	size_t i = 0;

	memset(out, 0, MSEED_FRAMES * 64u);
	for (unsigned f = 0; f < MSEED_FRAMES && i < n; f++) {
		uint8_t *fr = out + 64u * f;
		uint32_t w0 = 0;
		unsigned word = 1;

		if (f == 0) {
			put32(fr + 4, (uint32_t)x[0]);
			word = 3;
		}
		for (; word < 16 && i < n; word++) {
			unsigned c = 0;
			for (; c < sizeof(forms) / sizeof(forms[0]); c++) {
				if (forms[c].count > n - i) {
					continue;
				}
				unsigned k = 0;
				for (; k < forms[c].count; k++) {
					size_t j = i + k;
					int64_t d = j ? (int64_t)x[j] - x[j - 1] : have_prev ? (int64_t)x[0] - prev : 0;
					if (!fits(d, forms[c].bits)) {
						break;
					}
				}
				if (k == forms[c].count) {
					break;
				}
			}
			if (c == sizeof(forms) / sizeof(forms[0])) {
				return MSEED_EOVERFLOW;
			}
			uint32_t body = 0, mask = forms[c].bits == 32 ? 0xFFFFFFFFu : ((uint32_t)1 << forms[c].bits) - 1;
			for (unsigned k = 0; k < forms[c].count; k++) {
				size_t j = i + k;
				int64_t d = j ? (int64_t)x[j] - x[j - 1] : have_prev ? (int64_t)x[0] - prev : 0;
				body = (body << forms[c].bits) | ((uint32_t)d & mask);
			}
			if (forms[c].dnib >= 0) {
				body |= (uint32_t)forms[c].dnib << 30;
			}
			put32(fr + 4u * word, body);
			w0 |= (uint32_t)forms[c].nib << (2u * (15u - word));
			i += forms[c].count;
		}
		put32(fr, w0);
	}
	put32(out + 8, (uint32_t)x[i - 1]);
	return (long)i;
}

static int32_t sext(uint32_t v, unsigned bits)
{
	uint32_t m = (uint32_t)1 << (bits - 1);

	v &= bits == 32 ? 0xFFFFFFFFu : ((uint32_t)1 << bits) - 1;
	return (int32_t)((v ^ m) - m);
}

int mseed_unsteim2(const uint8_t *frames, unsigned nframes, unsigned count, int32_t *x)
{
	unsigned got = 0;
	int32_t acc = 0;

	for (unsigned f = 0; f < nframes && got < count; f++) {
		const uint8_t *fr = frames + 64u * f;
		uint32_t w0 = get32(fr);
		for (unsigned word = f ? 1 : 3; word < 16 && got < count; word++) {
			uint32_t v = get32(fr + 4u * word), nib = (w0 >> (2u * (15u - word))) & 3u, d = v >> 30;
			unsigned cnt = 0, bits = 0;
			if (nib == 1) {
				cnt = 4, bits = 8;
			} else if (nib == 2) {
				cnt = d == 1 ? 1 : d == 2 ? 2 : d == 3 ? 3 : 0;
				bits = d == 1 ? 30 : d == 2 ? 15 : 10;
			} else if (nib == 3) {
				cnt = d == 0 ? 5 : d == 1 ? 6 : d == 2 ? 7 : 0;
				bits = d == 0 ? 6 : d == 1 ? 5 : 4;
			}
			for (unsigned k = 0; k < cnt && got < count; k++) {
				int32_t diff = sext(v >> (bits * (cnt - 1 - k)), bits);
				acc = got ? (int32_t)((uint32_t)acc + (uint32_t)diff) : (int32_t)get32(frames + 4);
				x[got++] = acc;
			}
		}
	}
	return got == count && count && x[count - 1] == (int32_t)get32(frames + 8) ? 0 : -1;
}

/* days since 1970 to the civil date (H. Hinnant's algorithm), and the day of the year */
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

static uint64_t at(const struct mseed_writer *w, uint64_t base_us, uint64_t index)
{
	/* base + index · den / num s, in µs, rounded a half up */
	return base_us + (index * 1000000u * w->den * 2u + w->num) / (2u * w->num);
}

int mseed_run(struct mseed_writer *w, uint64_t base_us, uint64_t first, const int32_t *x,
	      const uint8_t *quality, size_t n)
{
	uint8_t rec[MSEED_RECORD];
	size_t k = 0;

	while (k < n) {
		memset(rec, 0, sizeof(rec));
		long c = mseed_steim2(x + k, n - k, k ? x[k - 1] : 0, k > 0, rec + 64);
		if (c < 0) {
			return (int)c;
		}
		uint8_t q = 0;
		for (long j = 0; quality && j < c; j++) {
			q |= quality[k + (size_t)j] & (MSEED_CLIPPED | MSEED_GLITCHES);
		}
		uint64_t us = at(w, base_us, first + k);
		int64_t units = (int64_t)(us / 100u);
		int rem = (int)(us % 100u);
		if (rem >= 50) {
			units++, rem -= 100;
		}
		int64_t secs = units / 10000;
		unsigned tenths = (unsigned)(units % 10000);
		unsigned m, d, doy, s = (unsigned)(secs % 86400);
		int y;
		civil(secs / 86400, &y, &m, &d, &doy);
		char seq[8];
		snprintf(seq, sizeof(seq), "%06u", (unsigned)w->seq);
		memcpy(rec, seq, 6);
		rec[6] = 'D', rec[7] = ' ';
		memcpy(rec + 8, w->sta, 5);
		memcpy(rec + 13, w->loc, 2);
		memcpy(rec + 15, w->cha, 3);
		memcpy(rec + 18, w->net, 2);
		put16(rec + 20, (uint32_t)y);
		put16(rec + 22, doy);
		rec[24] = (uint8_t)(s / 3600), rec[25] = (uint8_t)(s / 60 % 60), rec[26] = (uint8_t)(s % 60);
		put16(rec + 28, tenths);
		put16(rec + 30, (uint32_t)c);
		put16(rec + 32, (uint16_t)w->factor);
		put16(rec + 34, (uint16_t)w->mult);
		rec[38] = q;
		rec[39] = 2;                            /* blockettes */
		put16(rec + 44, 64);                    /* the data's start */
		put16(rec + 46, 48);                    /* the first blockette */
		put16(rec + 48, 1000), put16(rec + 50, 56);
		rec[52] = 11, rec[53] = 1, rec[54] = 9; /* Steim-2, big-endian, 2^9 B */
		put16(rec + 56, 1001), put16(rec + 58, 0);
		rec[60] = 100, rec[61] = (uint8_t)(int8_t)rem;
		unsigned used = 0;
		for (unsigned f = 0; f < MSEED_FRAMES; f++) {
			used += get32(rec + 64 + 64 * f) != 0 || f == 0;
		}
		rec[63] = (uint8_t)used;
		if (w->write(w->ctx, rec) != 0) {
			return MSEED_EWRITE;
		}
		w->seq = w->seq == 999999u ? 1u : w->seq + 1u;
		k += (size_t)c;
	}
	return 0;
}

void mseed_channel_code(char out[4], uint64_t rate_num, uint64_t rate_den, const char *unit, char orient)
{
	double r = (double)rate_num / (double)rate_den;
	char u[16];
	unsigned n = 0;

	out[0] = r >= 1000 ? 'F' : r >= 250 ? 'C' : r >= 80 ? 'H' : r >= 10 ? 'B' : r > 1 ? 'M' :
		 r >= 0.5 ? 'L' : r >= 0.05 ? 'V' : r >= 0.005 ? 'U' : 'R';
	for (unsigned i = 0; unit && unit[i] && n < sizeof(u) - 1; i++) {
		char c = unit[i];
		if (c == ' ' || c == '^' || c == '*') {
			continue;
		}
		u[n++] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
	}
	u[n] = 0;
	out[1] = !strcmp(u, "m/s2") || !strcmp(u, "m/s/s") ? 'N' : !strcmp(u, "m/s") ? 'H' :
		 !strcmp(u, "nt") || !strcmp(u, "t") ? 'F' :
		 !strcmp(u, "pa") || !strcmp(u, "hpa") || !strcmp(u, "kpa") || !strcmp(u, "bar") ||
		 !strcmp(u, "mbar") ? 'D' : !strcmp(u, "k") || !strcmp(u, "degc") || !strcmp(u, "c") ? 'K' : 'Y';
	out[2] = orient;
	out[3] = 0;
}

char mseed_orientation(const char *name, char fallback)
{
	size_t n = name ? strlen(name) : 0;

	while (n && name[n - 1] == ' ') {
		n--;
	}
	char c = n ? name[n - 1] : 0;
	c = (char)(c >= 'a' && c <= 'z' ? c - 32 : c);
	return c == 'Z' || c == 'N' || c == 'E' ? c : c == 'X' ? '1' : c == 'Y' ? '2' : fallback;
}

struct out {
	char *buf;
	size_t cap, len;
};

static void put(struct out *o, const char *s)
{
	for (; *s; s++, o->len++) {
		if (o->len + 1 < o->cap) {
			o->buf[o->len] = *s;
		}
	}
	if (o->cap) {
		o->buf[o->len < o->cap ? o->len : o->cap - 1] = 0;
	}
}

static void esc(struct out *o, const char *s)
{
	char c[2] = { 0, 0 };

	for (; s && *s; s++) {
		if (*s == '&') {
			put(o, "&amp;");
		} else if (*s == '<') {
			put(o, "&lt;");
		} else if (*s == '>') {
			put(o, "&gt;");
		} else {
			c[0] = *s;
			put(o, c);
		}
	}
}

static void fmt(struct out *o, const char *f, double v)
{
	char t[64];

	snprintf(t, sizeof(t), f, v);
	put(o, t);
}

size_t mseed_stationxml(char *buf, size_t cap, const char *net, const char *sta, const char *site,
			const char *start, double lat, double lon, double elev,
			const struct mseed_xml_channel *ch, unsigned n)
{
	struct out o = { buf, cap, 0 };

	put(&o, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
		"<FDSNStationXML xmlns=\"http://www.fdsn.org/xml/station/1\" schemaVersion=\"1.2\">\n"
		" <Source>NIC-SDK</Source>\n <Created>");
	esc(&o, start);
	put(&o, "</Created>\n <Network code=\"");
	esc(&o, net);
	put(&o, "\">\n  <Station code=\"");
	esc(&o, sta);
	put(&o, "\" startDate=\"");
	esc(&o, start);
	put(&o, "\">\n");
	fmt(&o, "   <Latitude>%.7f</Latitude>\n", lat);
	fmt(&o, "   <Longitude>%.7f</Longitude>\n", lon);
	fmt(&o, "   <Elevation>%g</Elevation>\n", elev);
	put(&o, "   <Site><Name>");
	esc(&o, site);
	put(&o, "</Name></Site>\n");
	for (unsigned i = 0; i < n; i++) {
		put(&o, "   <Channel code=\"");
		esc(&o, ch[i].cha);
		put(&o, "\" locationCode=\"");
		esc(&o, ch[i].loc);
		put(&o, "\" startDate=\"");
		esc(&o, start);
		put(&o, "\">\n");
		fmt(&o, "    <Latitude>%.7f</Latitude>\n", lat);
		fmt(&o, "    <Longitude>%.7f</Longitude>\n", lon);
		fmt(&o, "    <Elevation>%g</Elevation>\n", elev);
		put(&o, "    <Depth>0</Depth>\n");
		fmt(&o, "    <SampleRate>%g</SampleRate>\n", (double)ch[i].rate_num / (double)ch[i].rate_den);
		put(&o, "    <Response>\n     <InstrumentSensitivity>\n");
		fmt(&o, "      <Value>%.10g</Value>\n", ch[i].counts_per_unit);
		put(&o, "      <Frequency>1</Frequency>\n      <InputUnits><Name>");
		esc(&o, ch[i].unit && ch[i].unit[0] ? ch[i].unit : "count");
		put(&o, "</Name></InputUnits>\n      <OutputUnits><Name>count</Name></OutputUnits>\n"
			"     </InstrumentSensitivity>\n    </Response>\n   </Channel>\n");
	}
	put(&o, "  </Station>\n </Network>\n</FDSNStationXML>\n");
	return o.len;
}
