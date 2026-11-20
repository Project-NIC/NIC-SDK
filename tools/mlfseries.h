/* NIC-SDK — one channel's series out of a series of MLF files, through the program's glue.
 * MLF's core does not say what a record is — the profile does, named by the mark's letter — so the
 * SDK walks the files, the configs and the sections in force, and the program's glue reads the
 * segments: each program links its own `mlf_glue` (an example: tests/glue_example.c). The
 * exporters (tools/mlf2mseed.c, tools/mlf2iaga.c) are built on it. Its twin: tools/mlfseries.py. */
#ifndef MLFSERIES_H
#define MLFSERIES_H
#include <stddef.h>
#include <stdint.h>
#include "../mlf/mlf.h"

struct mlf_channel {
	char name[9], unit[9];
	int8_t exp10;
	int16_t mantissa;               /* 0 counts as 1; physical = (raw + add) · mantissa · 10^exp10 */
	int32_t add;
	uint32_t rate_num, rate_den;    /* samples a second, num / den */
};

struct mlf_station {
	int known;
	char name[33];
	double lat, lon, elev;          /* degrees, metres */
};

/* A sample: its number t · rate since 1970 UTC, its raw value, SEED's data-quality bits
 * (0x02 clipped, 0x08 glitches; 0 clean). */
typedef void (*mlf_emit_fn)(void *ctx, int64_t index, int32_t raw, uint8_t quality);

struct mlf_glue {
	char letter;                    /* the profile it reads */
	/* the channel named, from the sections in force; 0, or −1 none such */
	int (*channel)(const mlf_sections *s, const char *name, struct mlf_channel *out);
	/* the station, from the sections in force; 0 known, −1 not */
	int (*station)(const mlf_sections *s, struct mlf_station *out);
	/* a segment's samples of the channel named, through emit — an even run's time from its first
	 * second and place (seg->run, seg->place); 0, or −1 a segment not read */
	int (*samples)(const mlf_sections *s, const mlf_object *seg, const char *name, mlf_emit_fn emit, void *ctx);
};

extern const struct mlf_glue mlf_glue;  /* the program's */

struct mlf_sample {
	int64_t index;
	int32_t raw;
	uint8_t quality;
	uint32_t order;                 /* as written: of two for one index the later is kept */
};

/* The files in order: the channel's description and station from the first segment's sections,
 * its samples sorted by number, malloc'd. 0, or −1 with a message on stderr. */
int mlf_series(char *const *paths, int n, const char *name, struct mlf_channel *ch, struct mlf_station *st,
	       struct mlf_sample **out, size_t *count);

#endif
